/* gfx.c - motor gráfico por software (sin dependencias). Toda la interfaz del launcher se dibuja
 * aquí en un lienzo ARGB; cada plataforma (SDL2, Android, PS4...) solo tiene que mostrarlo. */
#include "gfx.h"
#include "font.h"
#include "png.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static inline int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

/* ------------------------------------------------------------------ mezcla */
static inline uint32_t blend_px(uint32_t dst, uint32_t src, int sa) { /* sa: alfa efectivo 0..255 */
	if (sa <= 0) return dst;
	uint32_t da = dst >> 24;
	int sr = (src >> 16) & 255, sg = (src >> 8) & 255, sb = src & 255;
	if (da == 255) {
		if (sa >= 255) return 0xFF000000u | (src & 0xFFFFFF);
		int ia = 255 - sa;
		int r = (sr * sa + (int) ((dst >> 16) & 255) * ia + 127) / 255;
		int g = (sg * sa + (int) ((dst >> 8) & 255) * ia + 127) / 255;
		int b = (sb * sa + (int) (dst & 255) * ia + 127) / 255;
		return 0xFF000000u | (r << 16) | (g << 8) | b;
	}
	int oa = sa + (int) (da * (255 - sa) / 255);
	if (oa <= 0) return 0;
	int dr = (dst >> 16) & 255, dg = (dst >> 8) & 255, db = dst & 255;
	int dw = (int) (da * (255 - sa) / 255);
	int r = (sr * sa + dr * dw) / oa, g = (sg * sa + dg * dw) / oa, b = (sb * sa + db * dw) / oa;
	return ((uint32_t) oa << 24) | (r << 16) | (g << 8) | b;
}

static inline void put_cov(Canvas* c, int x, int y, uint32_t color, float cov) {
	if (x < 0 || y < 0 || x >= c->w || y >= c->h || cov <= 0.0f) return;
	int a = (int) ((color >> 24) * (cov > 1.0f ? 1.0f : cov) + 0.5f);
	uint32_t* p = &c->px[(size_t) y * c->w + x];
	*p = blend_px(*p, color, a);
}

/* ------------------------------------------------------------------ lienzo */
Canvas* cv_new(int w, int h) {
	Canvas* c = malloc(sizeof(Canvas));
	if (!c) return NULL;
	c->w = w; c->h = h;
	c->px = calloc((size_t) w * h, 4);
	if (!c->px) { free(c); return NULL; }
	return c;
}
void cv_free(Canvas* c) { if (c) { free(c->px); free(c); } }
void cv_clear(Canvas* c, uint32_t color) { for (size_t i = 0; i < (size_t) c->w * c->h; i++) c->px[i] = color; }
Canvas* cv_copy(const Canvas* s) {
	Canvas* c = cv_new(s->w, s->h);
	if (c) memcpy(c->px, s->px, (size_t) s->w * s->h * 4);
	return c;
}
Canvas* cv_load_png(const char* path) {
	int w, h;
	uint32_t* px = png_load_file(path, &w, &h);
	if (!px) return NULL;
	Canvas* c = malloc(sizeof(Canvas));
	if (!c) { free(px); return NULL; }
	c->w = w; c->h = h; c->px = px;
	return c;
}

/* Remuestreo separable: área (promedio) al reducir y lineal al ampliar; con alfa premultiplicado. */
typedef struct { int i0, n; float* w; } Taps;
static Taps* make_taps(int src, int dst) {
	Taps* t = malloc(sizeof(Taps) * dst);
	float scale = (float) src / dst;
	for (int d = 0; d < dst; d++) {
		float a, b;
		if (scale > 1.0f) { a = d * scale; b = (d + 1) * scale; }
		else { float c = (d + 0.5f) * scale - 0.5f; a = c; b = c + 1.0f; }
		int i0, i1;
		if (scale > 1.0f) { i0 = (int) floorf(a); i1 = (int) ceilf(b); }
		else { i0 = (int) floorf(a); i1 = i0 + 2; }
		int n = i1 - i0;
		t[d].w = malloc(sizeof(float) * (n > 0 ? n : 1));
		t[d].i0 = i0; t[d].n = n;
		float sum = 0;
		for (int k = 0; k < n; k++) {
			float wgt;
			if (scale > 1.0f) {
				float lo = fmaxf((float) (i0 + k), a), hi = fminf((float) (i0 + k + 1), b);
				wgt = hi > lo ? hi - lo : 0;
			} else {
				float c = (d + 0.5f) * scale - 0.5f;
				wgt = 1.0f - fabsf((float) (i0 + k) - c);
				if (wgt < 0) wgt = 0;
			}
			t[d].w[k] = wgt; sum += wgt;
		}
		if (sum > 0) for (int k = 0; k < n; k++) t[d].w[k] /= sum;
		if (i0 < 0 || i0 + n > src) { /* recorta a los bordes (repite el último píxel) */
			float* nw = calloc((size_t) (n > 0 ? n : 1), sizeof(float));
			int base = i0;
			for (int k = 0; k < n; k++) {
				int idx = clampi(base + k, 0, src - 1);
				(void) idx;
			}
			free(nw);
		}
	}
	return t;
}
static void free_taps(Taps* t, int n) { for (int i = 0; i < n; i++) free(t[i].w); free(t); }

Canvas* cv_resize(const Canvas* s, int nw, int nh) {
	if (nw < 1) nw = 1;
	if (nh < 1) nh = 1;
	Canvas* out = cv_new(nw, nh);
	if (!out) return NULL;
	size_t tmpn = (size_t) nw * s->h * 4;
	float* tmp = malloc(tmpn * sizeof(float));
	if (!tmp) { cv_free(out); return NULL; }
	Taps* tx = make_taps(s->w, nw);
	Taps* ty = make_taps(s->h, nh);
	for (int y = 0; y < s->h; y++) {
		const uint32_t* row = s->px + (size_t) y * s->w;
		for (int x = 0; x < nw; x++) {
			float a = 0, r = 0, g = 0, b = 0;
			for (int k = 0; k < tx[x].n; k++) {
				int sx = clampi(tx[x].i0 + k, 0, s->w - 1);
				uint32_t p = row[sx];
				float wa = tx[x].w[k] * (float) (p >> 24);
				a += wa; r += wa * ((p >> 16) & 255); g += wa * ((p >> 8) & 255); b += wa * (p & 255);
			}
			float* o = tmp + ((size_t) y * nw + x) * 4;
			o[0] = a; o[1] = r; o[2] = g; o[3] = b;   /* r,g,b ya ponderados por alfa */
		}
	}
	for (int y = 0; y < nh; y++) {
		for (int x = 0; x < nw; x++) {
			float a = 0, r = 0, g = 0, b = 0;
			for (int k = 0; k < ty[y].n; k++) {
				int sy = clampi(ty[y].i0 + k, 0, s->h - 1);
				const float* p = tmp + ((size_t) sy * nw + x) * 4;
				float w = ty[y].w[k];
				a += w * p[0]; r += w * p[1]; g += w * p[2]; b += w * p[3];
			}
			uint32_t pa = (uint32_t) clampi((int) (a + 0.5f), 0, 255);
			uint32_t pr = 0, pg = 0, pb = 0;
			if (a > 0.001f) {
				pr = (uint32_t) clampi((int) (r / a + 0.5f), 0, 255);
				pg = (uint32_t) clampi((int) (g / a + 0.5f), 0, 255);
				pb = (uint32_t) clampi((int) (b / a + 0.5f), 0, 255);
			}
			out->px[(size_t) y * nw + x] = (pa << 24) | (pr << 16) | (pg << 8) | pb;
		}
	}
	free(tmp);
	free_taps(tx, nw);
	free_taps(ty, nh);
	return out;
}

Canvas* cv_cover(const Canvas* s, int w, int h) {
	float sc = fmaxf((float) w / s->w, (float) h / s->h);
	int rw = (int) ceilf(s->w * sc), rh = (int) ceilf(s->h * sc);
	Canvas* big = cv_resize(s, rw, rh);
	if (!big) return NULL;
	Canvas* out = cv_new(w, h);
	if (out) {
		int ox = (rw - w) / 2, oy = (rh - h) / 2;
		for (int y = 0; y < h; y++) memcpy(out->px + (size_t) y * w, big->px + (size_t) (y + oy) * rw + ox, (size_t) w * 4);
	}
	cv_free(big);
	return out;
}

Canvas* cv_fit(const Canvas* s, int maxw, int maxh) {
	float sc = fminf((float) maxw / s->w, (float) maxh / s->h);
	return cv_resize(s, (int) (s->w * sc + 0.5f), (int) (s->h * sc + 0.5f));
}

/* Distancia con signo a un rectángulo redondeado centrado en el origen (semiancho hw, semialto hh). */
static inline float sd_rrect(float px, float py, float hw, float hh, float r) {
	float qx = fabsf(px) - (hw - r), qy = fabsf(py) - (hh - r);
	float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0;
	float inner = qx > qy ? qx : qy;
	return sqrtf(ox * ox + oy * oy) + (inner < 0 ? inner : 0) - r;
}

void cv_round_corners(Canvas* c, int radius) {
	float hw = c->w / 2.0f, hh = c->h / 2.0f;
	for (int y = 0; y < c->h; y++) {
		for (int x = 0; x < c->w; x++) {
			if (x >= radius && x < c->w - radius) continue;
			if (y >= radius && y < c->h - radius) continue;
			float d = sd_rrect(x + 0.5f - hw, y + 0.5f - hh, hw, hh, (float) radius);
			float cov = clampf(0.5f - d, 0.0f, 1.0f);
			if (cov >= 1.0f) continue;
			uint32_t* p = &c->px[(size_t) y * c->w + x];
			*p = (*p & 0x00FFFFFFu) | ((uint32_t) ((*p >> 24) * cov + 0.5f) << 24);
		}
	}
}

void cv_shade(Canvas* c, int alpha) {
	for (size_t i = 0; i < (size_t) c->w * c->h; i++) {
		uint32_t p = c->px[i];
		uint32_t a = p >> 24;
		if (!a) continue;
		int ia = 255 - alpha;
		c->px[i] = (a << 24) | ((((p >> 16) & 255) * ia / 255) << 16) | ((((p >> 8) & 255) * ia / 255) << 8) | ((p & 255) * ia / 255);
	}
}

/* --------------------------------------------------------------- primitivas */
void gfx_rect(Canvas* c, Rect r, uint32_t color) {
	int x0 = clampi(r.x, 0, c->w), y0 = clampi(r.y, 0, c->h);
	int x1 = clampi(r.x + r.w, 0, c->w), y1 = clampi(r.y + r.h, 0, c->h);
	int a = color >> 24;
	if (a == 0) return;
	for (int y = y0; y < y1; y++) {
		uint32_t* p = c->px + (size_t) y * c->w;
		if (a == 255) { for (int x = x0; x < x1; x++) p[x] = color; }
		else { for (int x = x0; x < x1; x++) p[x] = blend_px(p[x], color, a); }
	}
}

void gfx_rrect(Canvas* c, Rect r, int radius, uint32_t color) {
	if (radius <= 0) { gfx_rect(c, r, color); return; }
	if (radius * 2 > r.w) radius = r.w / 2;
	if (radius * 2 > r.h) radius = r.h / 2;
	float hw = r.w / 2.0f, hh = r.h / 2.0f;
	int a = color >> 24;
	for (int y = 0; y < r.h; y++) {
		int cy = r.y + y;
		if (cy < 0 || cy >= c->h) continue;
		for (int x = 0; x < r.w; x++) {
			int cx = r.x + x;
			if (cx < 0 || cx >= c->w) continue;
			float cov = 1.0f;
			if ((x < radius || x >= r.w - radius) && (y < radius || y >= r.h - radius)) {
				cov = clampf(0.5f - sd_rrect(x + 0.5f - hw, y + 0.5f - hh, hw, hh, (float) radius), 0.0f, 1.0f);
				if (cov <= 0.0f) continue;
			}
			uint32_t* p = &c->px[(size_t) cy * c->w + cx];
			*p = blend_px(*p, color, (int) (a * cov + 0.5f));
		}
	}
}

void gfx_rrect_stroke(Canvas* c, Rect r, int radius, int t, uint32_t color) {
	if (radius * 2 > r.w) radius = r.w / 2;
	if (radius * 2 > r.h) radius = r.h / 2;
	float hw = r.w / 2.0f, hh = r.h / 2.0f;
	int a = color >> 24;
	for (int y = 0; y < r.h; y++) {
		int cy = r.y + y;
		if (cy < 0 || cy >= c->h) continue;
		for (int x = 0; x < r.w; x++) {
			int cx = r.x + x;
			if (cx < 0 || cx >= c->w) continue;
			if (x >= t + radius && x < r.w - t - radius && y >= t && y < r.h - t) continue;   /* interior */
			if (y >= t + radius && y < r.h - t - radius && x >= t && x < r.w - t) continue;
			float d = sd_rrect(x + 0.5f - hw, y + 0.5f - hh, hw, hh, (float) radius);
			float cov = clampf(0.5f - d, 0.0f, 1.0f) * clampf(d + t + 0.5f, 0.0f, 1.0f);
			if (cov <= 0.0f) continue;
			uint32_t* p = &c->px[(size_t) cy * c->w + cx];
			*p = blend_px(*p, color, (int) (a * cov + 0.5f));
		}
	}
}

void gfx_circle(Canvas* c, float cx, float cy, float rad, uint32_t color) {
	int x0 = (int) floorf(cx - rad - 1), x1 = (int) ceilf(cx + rad + 1);
	int y0 = (int) floorf(cy - rad - 1), y1 = (int) ceilf(cy + rad + 1);
	for (int y = y0; y <= y1; y++)
		for (int x = x0; x <= x1; x++) {
			float d = sqrtf((x + 0.5f - cx) * (x + 0.5f - cx) + (y + 0.5f - cy) * (y + 0.5f - cy)) - rad;
			put_cov(c, x, y, color, clampf(0.5f - d, 0.0f, 1.0f));
		}
}

void gfx_ring(Canvas* c, float cx, float cy, float rad, float t, uint32_t color) {
	int x0 = (int) floorf(cx - rad - 1), x1 = (int) ceilf(cx + rad + 1);
	int y0 = (int) floorf(cy - rad - 1), y1 = (int) ceilf(cy + rad + 1);
	for (int y = y0; y <= y1; y++)
		for (int x = x0; x <= x1; x++) {
			float d = sqrtf((x + 0.5f - cx) * (x + 0.5f - cx) + (y + 0.5f - cy) * (y + 0.5f - cy)) - rad;
			float cov = clampf(0.5f - d, 0.0f, 1.0f) * clampf(d + t + 0.5f, 0.0f, 1.0f);
			put_cov(c, x, y, color, cov);
		}
}

/* Polígono con suavizado: 4 sub-líneas por fila y cobertura horizontal fraccionaria. */
void gfx_poly(Canvas* c, const float* xy, int n, uint32_t color) {
	if (n < 3) return;
	float ymin = xy[1], ymax = xy[1];
	for (int i = 1; i < n; i++) { if (xy[2 * i + 1] < ymin) ymin = xy[2 * i + 1]; if (xy[2 * i + 1] > ymax) ymax = xy[2 * i + 1]; }
	int y0 = clampi((int) floorf(ymin), 0, c->h - 1), y1 = clampi((int) ceilf(ymax), 0, c->h - 1);
	float* cov = calloc((size_t) c->w + 2, sizeof(float));
	float* xs = malloc(sizeof(float) * (size_t) (n + 2));
	if (!cov || !xs) { free(cov); free(xs); return; }
	const int SS = 4;
	for (int y = y0; y <= y1; y++) {
		int touched0 = c->w, touched1 = -1;
		for (int s = 0; s < SS; s++) {
			float sy = y + (s + 0.5f) / SS;
			int cnt = 0;
			for (int i = 0; i < n; i++) {
				int j = (i + 1) % n;
				float ax = xy[2 * i], ay = xy[2 * i + 1], bx = xy[2 * j], by = xy[2 * j + 1];
				if ((ay <= sy && by > sy) || (by <= sy && ay > sy)) xs[cnt++] = ax + (sy - ay) / (by - ay) * (bx - ax);
			}
			for (int i = 1; i < cnt; i++) { float v = xs[i]; int k = i - 1; while (k >= 0 && xs[k] > v) { xs[k + 1] = xs[k]; k--; } xs[k + 1] = v; }
			for (int i = 0; i + 1 < cnt; i += 2) {
				float a = clampf(xs[i], 0, (float) c->w), b = clampf(xs[i + 1], 0, (float) c->w);
				if (b <= a) continue;
				int ia = (int) floorf(a), ib = (int) floorf(b);
				if (ia < touched0) touched0 = ia;
				if (ib > touched1) touched1 = ib;
				if (ia == ib) { cov[ia] += (b - a) / SS; continue; }
				cov[ia] += (ia + 1 - a) / SS;
				for (int x = ia + 1; x < ib; x++) cov[x] += 1.0f / SS;
				if (ib < c->w) cov[ib] += (b - ib) / SS;
			}
		}
		for (int x = touched0; x <= touched1 && x < c->w; x++) { if (cov[x] > 0) put_cov(c, x, y, color, cov[x]); cov[x] = 0; }
	}
	free(cov); free(xs);
}

void gfx_line(Canvas* c, float x0, float y0, float x1, float y1, float t, uint32_t color) {
	float dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy);
	if (len < 0.001f) return;
	float nx = -dy / len * t / 2, ny = dx / len * t / 2;
	float q[8] = {x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny};
	gfx_poly(c, q, 4, color);
}

/* ------------------------------------------------------------------ imágenes */
void gfx_blit(Canvas* dst, const Canvas* src, int x, int y, int alpha) {
	for (int sy = 0; sy < src->h; sy++) {
		int dy = y + sy;
		if (dy < 0 || dy >= dst->h) continue;
		for (int sx = 0; sx < src->w; sx++) {
			int dx = x + sx;
			if (dx < 0 || dx >= dst->w) continue;
			uint32_t s = src->px[(size_t) sy * src->w + sx];
			int a = (int) (s >> 24);
			if (alpha < 255) a = a * alpha / 255;
			if (!a) continue;
			uint32_t* d = &dst->px[(size_t) dy * dst->w + dx];
			*d = blend_px(*d, s, a);
		}
	}
}

void gfx_blit_scaled(Canvas* dst, const Canvas* src, Rect to, int alpha) {
	if (to.w <= 0 || to.h <= 0) return;
	if (to.w == src->w && to.h == src->h) { gfx_blit(dst, src, to.x, to.y, alpha); return; }
	int x0 = clampi(to.x, 0, dst->w), x1 = clampi(to.x + to.w, 0, dst->w);
	int y0 = clampi(to.y, 0, dst->h), y1 = clampi(to.y + to.h, 0, dst->h);
	float fx = (float) src->w / to.w, fy = (float) src->h / to.h;
	for (int y = y0; y < y1; y++) {
		float sy = (y - to.y + 0.5f) * fy - 0.5f;
		int iy = (int) floorf(sy);
		float ty = sy - iy;
		int ya = clampi(iy, 0, src->h - 1), yb = clampi(iy + 1, 0, src->h - 1);
		for (int x = x0; x < x1; x++) {
			float sx = (x - to.x + 0.5f) * fx - 0.5f;
			int ix = (int) floorf(sx);
			float tx = sx - ix;
			int xa = clampi(ix, 0, src->w - 1), xb = clampi(ix + 1, 0, src->w - 1);
			uint32_t p00 = src->px[(size_t) ya * src->w + xa], p10 = src->px[(size_t) ya * src->w + xb];
			uint32_t p01 = src->px[(size_t) yb * src->w + xa], p11 = src->px[(size_t) yb * src->w + xb];
			float w00 = (1 - tx) * (1 - ty), w10 = tx * (1 - ty), w01 = (1 - tx) * ty, w11 = tx * ty;
			float a = w00 * (p00 >> 24) + w10 * (p10 >> 24) + w01 * (p01 >> 24) + w11 * (p11 >> 24);
			if (a < 0.5f) continue;
			float r = w00 * (p00 >> 24) * ((p00 >> 16) & 255) + w10 * (p10 >> 24) * ((p10 >> 16) & 255) + w01 * (p01 >> 24) * ((p01 >> 16) & 255) + w11 * (p11 >> 24) * ((p11 >> 16) & 255);
			float g = w00 * (p00 >> 24) * ((p00 >> 8) & 255) + w10 * (p10 >> 24) * ((p10 >> 8) & 255) + w01 * (p01 >> 24) * ((p01 >> 8) & 255) + w11 * (p11 >> 24) * ((p11 >> 8) & 255);
			float b = w00 * (p00 >> 24) * (p00 & 255) + w10 * (p10 >> 24) * (p10 & 255) + w01 * (p01 >> 24) * (p01 & 255) + w11 * (p11 >> 24) * (p11 & 255);
			uint32_t s = ARGB(0, clampi((int) (r / a + 0.5f), 0, 255), clampi((int) (g / a + 0.5f), 0, 255), clampi((int) (b / a + 0.5f), 0, 255));
			int ea = (int) (a + 0.5f);
			if (alpha < 255) ea = ea * alpha / 255;
			uint32_t* d = &dst->px[(size_t) y * dst->w + x];
			*d = blend_px(*d, s, ea);
		}
	}
}

/* ---------------------------------------------------------------------- texto */
static short g_lut[256];
static int g_lut_ready;

static const FontGlyph* find_glyph(int cp) {
	if (!g_lut_ready) {
		for (int i = 0; i < 256; i++) g_lut[i] = -1;
		for (int i = 0; i < font_glyph_count; i++) if (font_glyphs[i].cp < 256) g_lut[font_glyphs[i].cp] = (short) i;
		g_lut_ready = 1;
	}
	if (cp >= 0 && cp < 256 && g_lut[cp] >= 0) return &font_glyphs[g_lut[cp]];
	for (int i = 0; i < font_glyph_count; i++) if (font_glyphs[i].cp == cp) return &font_glyphs[i];
	return cp == '?' ? NULL : find_glyph('?');
}

static int next_cp(const char** s) {
	const unsigned char* p = (const unsigned char*) *s;
	int c = *p;
	if (!c) return 0;
	if (c < 0x80) { *s += 1; return c; }
	if ((c >> 5) == 6 && p[1]) { *s += 2; return ((c & 31) << 6) | (p[1] & 63); }
	if ((c >> 4) == 14 && p[1] && p[2]) { *s += 3; return ((c & 15) << 12) | ((p[1] & 63) << 6) | (p[2] & 63); }
	if ((c >> 3) == 30 && p[1] && p[2] && p[3]) { *s += 4; return '?'; }
	*s += 1;
	return '?';
}

int gfx_text_width(const char* s, int size, int flags) {
	float scale = (float) size / font_em * ((flags & TX_BOLD) ? 1.0f : 0.96f), w = 0;
	int cp;
	while ((cp = next_cp(&s))) { const FontGlyph* g = find_glyph(cp); if (g) w += g->adv * scale; }
	return (int) (w + 0.5f);
}

static float sample_sdf(const FontGlyph* g, float lx, float ly) {
	/* lx,ly en píxeles del atlas dentro del glifo; bilineal con el valor 0 fuera */
	float fx = lx - 0.5f, fy = ly - 0.5f;
	int ix = (int) floorf(fx), iy = (int) floorf(fy);
	float tx = fx - ix, ty = fy - iy, v[4];
	for (int k = 0; k < 4; k++) {
		int xx = ix + (k & 1), yy = iy + (k >> 1);
		v[k] = (xx < 0 || yy < 0 || xx >= g->w || yy >= g->h) ? 0.0f : (float) font_atlas[(size_t) (g->ay + yy) * font_atlas_w + g->ax + xx];
	}
	return (v[0] * (1 - tx) + v[1] * tx) * (1 - ty) + (v[2] * (1 - tx) + v[3] * tx) * ty;
}

int gfx_text(Canvas* c, const char* s, int x, int y, int size, uint32_t color, int flags) {
	int width = gfx_text_width(s, size, flags);
	if (flags & TX_CENTER) x -= width / 2;
	else if (flags & TX_RIGHT) x -= width;
	float scale = (float) size / font_em;
	float adv_scale = scale * ((flags & TX_BOLD) ? 1.0f : 0.96f);   /* texto fino: algo más junto */
	float baseline = (flags & TX_VMID) ? y + size * 0.36f : y + font_ascent * scale;
	float shear = (flags & TX_ITALIC) ? 0.22f : 0.0f;
	float delta = (flags & TX_BOLD) ? 0.01f * size : -0.035f * size;   /* grosor: ± desplazamiento del borde */
	float pen = (float) x;
	int ca = (int) (color >> 24);
	int cp;
	while ((cp = next_cp(&s))) {
		const FontGlyph* g = find_glyph(cp);
		if (!g) continue;
		if (g->w > 0) {
			float gx = pen + g->x0 * scale, gy = baseline + g->y0 * scale;
			float gw = g->w * scale, gh = g->h * scale;
			int px0 = (int) floorf(gx), px1 = (int) ceilf(gx + gw + shear * gh);
			int py0 = (int) floorf(gy), py1 = (int) ceilf(gy + gh);
			for (int py = clampi(py0, 0, c->h - 1); py <= clampi(py1, 0, c->h - 1); py++) {
				float hgt = baseline - (py + 0.5f);
				for (int px = clampi(px0, 0, c->w - 1); px <= clampi(px1, 0, c->w - 1); px++) {
					float lx = (px + 0.5f - pen - shear * hgt) / scale - g->x0;
					float ly = (py + 0.5f - baseline) / scale - g->y0;
					if (lx < -1 || ly < -1 || lx > g->w + 1 || ly > g->h + 1) continue;
					float v = sample_sdf(g, lx, ly);
					float d = (v - 128.0f) * (font_spread / 127.0f) * scale;
					float cov = clampf(d + delta + 0.5f, 0.0f, 1.0f);
					if (cov <= 0.0f) continue;
					uint32_t* p = &c->px[(size_t) py * c->w + px];
					*p = blend_px(*p, color, (int) (ca * cov + 0.5f));
				}
			}
		}
		pen += g->adv * adv_scale;
	}
	return width;
}
