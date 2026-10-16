/* app_ui.c - todas las pantallas del launcher, dibujadas por software en un lienzo 1920x1080. */
#include "app_internal.h"
#include "sys.h"
#include "debug_log.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define C_WHITE RGB(255, 255, 255)
#define C_SOFT RGB(215, 222, 240)
#define C_MUTED RGB(140, 150, 178)
#define C_GREEN RGB(90, 220, 140)
#define C_RED RGB(255, 100, 110)
#define C_GOLD RGB(255, 205, 60)
#define C_DARK RGB(10, 14, 30)

static const char* APP_TITLE = "SONIC ADVANCE TRILOGY";
static const char* APP_VERSION = "2.0.0";

/* ------------------------------------------------------------------ geometría */
Rect ui_card_rect(const App* a, int i) { (void) a; return rect_make(60 + i * 620, 205, 560, 315); }
Rect ui_battle_slot_rect(void) { return rect_make(60, 555, 540, 370); }
Rect ui_btn_rect(int j) {
	static const int w[4] = {360, 330, 260, 200};
	int x = 60;
	for (int k = 0; k < j; k++) x += w[k] + 24;
	return rect_make(x, 945, w[j], 70);
}
static Rect settings_panel(void) { return rect_make(490, 40, 940, 1000); }
Rect ui_settings_row(int k) { Rect p = settings_panel(); return rect_make(p.x + 40, p.y + 120 + k * 92, p.w - 80, 80); }
static Rect controls_panel(void) { return rect_make(160, 40, 1600, 1000); }
Rect ui_ctrl_row_rect(int k) { Rect p = controls_panel(); return rect_make(p.x + 40, p.y + 150 + k * 47, p.w - 80, 43); }
Rect ui_ctrl_cell_rect(int k, int col) {
	Rect r = ui_ctrl_row_rect(k);
	static const int offsets[3] = {350, 740, 1130};
	if (col < 0 || col > 2) col = 0;
	return rect_make(r.x + offsets[col], r.y, 350, r.h);
}
Rect ui_pause_rect(int i) { return rect_make(100, 200 + i * 98, 660, 82); }
Rect ui_slot_rect(int n) { return rect_make(100 + (n % 4) * 440, 200 + (n / 4) * 395, 400, 365); }
Rect ui_modal_ok(void) { return rect_make(UI_W / 2 - 140, 640, 280, 70); }
Rect ui_modal_yes(void) { return rect_make(UI_W / 2 - 300, 640, 260, 70); }
Rect ui_modal_no(void) { return rect_make(UI_W / 2 + 40, 640, 260, 70); }

/* ------------------------------------------------------------------ recursos */
typedef struct { const char* want; char found[PATH_MAX_APP]; const char* dir; } FindCtx;
static void find_cb(const char* name, void* ctx) {
	FindCtx* f = ctx;
	if (f->found[0]) return;
	if (sys_strcasecmp(name, f->want) == 0) snprintf(f->found, PATH_MAX_APP, "%s/%s", f->dir, name);
}
static Canvas* load_png_named(const char* dir, const char* stem) {
	char want[128];
	snprintf(want, sizeof(want), "%s.png", stem);
	FindCtx f = {want, "", dir};
	sys_list_dir(dir, find_cb, &f);
	return f.found[0] ? cv_load_png(f.found) : NULL;
}

static Canvas* make_gradient(uint32_t top, uint32_t bottom) {
	Canvas* c = cv_new(UI_W, UI_H);
	for (int y = 0; y < UI_H; y++) {
		float t = y / (float) (UI_H - 1);
		int r = (int) (((top >> 16) & 255) * (1 - t) + ((bottom >> 16) & 255) * t);
		int g = (int) (((top >> 8) & 255) * (1 - t) + ((bottom >> 8) & 255) * t);
		int b = (int) ((top & 255) * (1 - t) + (bottom & 255) * t);
		gfx_rect(c, rect_make(0, y, UI_W, 1), RGB(r, g, b));
	}
	gfx_circle(c, UI_W - 120, 120, 520, ARGB(26, 40, 110, 255));
	gfx_circle(c, 120, UI_H - 80, 420, ARGB(14, 255, 190, 40));
	return c;
}

static Canvas* make_placeholder(const GameInfo* g, int w, int h) {
	Canvas* c = cv_new(w, h);
	uint32_t ac = g->accent;
	for (int y = 0; y < h; y++) {
		float k = 0.62f - 0.42f * (y / (float) (h - 1));
		gfx_rect(c, rect_make(0, y, w, 1), RGB((int) (((ac >> 16) & 255) * k), (int) (((ac >> 8) & 255) * k), (int) ((ac & 255) * k)));
	}
	float s = h / 315.0f;
	gfx_ring(c, w * 0.80f, h * 0.42f, 95 * s, 12 * s, RGB(255, 205, 60));
	gfx_text(c, g->title, (int) (w * 0.06f), (int) (h * 0.30f), (int) (46 * s), C_WHITE, TX_BOLD);
	char tip[128];
	snprintf(tip, sizeof(tip), "Añade assets/games/%s.png", g->art);
	gfx_text(c, tip, (int) (w * 0.06f), h - (int) (40 * s), (int) (19 * s), RGB(235, 240, 255), 0);
	return c;
}

static Canvas* make_battle_placeholder(int w, int h) {
	Canvas* c = cv_new(w, h);
	if (!c) return NULL;
	for (int y = 0; y < h; y++) {
		float t = y / (float) (h - 1);
		int r = (int) (7 + 10 * t);
		int g = (int) (14 + 18 * t);
		int b = (int) (34 + 25 * t);
		gfx_rect(c, rect_make(0, y, w, 1), RGB(r, g, b));
	}
	/* Ilustración abstracta para reservar el hueco sin depender de una ROM ni de arte externo. */
	gfx_circle(c, w * 0.83f, h * 0.45f, h * 0.38f, ARGB(35, 35, 115, 245));
	gfx_ring(c, w * 0.83f, h * 0.45f, h * 0.28f, 7, RGB(255, 181, 46));
	gfx_line(c, w * 0.58f, 0, w * 0.43f, h, 5, ARGB(70, 255, 255, 255));
	gfx_line(c, w * 0.69f, 0, w * 0.54f, h, 2, ARGB(45, 255, 255, 255));
	gfx_text(c, "SONIC BATTLE", (int) (w * 0.055f), (int) (h * 0.28f), (int) (h * 0.16f), C_WHITE, TX_BOLD);
	gfx_text(c, "ESPACIO RESERVADO", (int) (w * 0.06f), (int) (h * 0.52f), (int) (h * 0.075f), RGB(255, 198, 65), TX_BOLD);
	gfx_text(c, "assets/games/sonicbattle.png", (int) (w * 0.06f), h - (int) (h * 0.11f), (int) (h * 0.055f), RGB(200, 211, 235), 0);
	return c;
}

void ui_load_assets(App* a) {
	Canvas* raw = load_png_named(a->assets_dir, "icon");
	if (raw) { a->icon = cv_fit(raw, 100, 100); cv_free(raw); }
	raw = load_png_named(a->assets_dir, "title");
	if (raw) { a->title = cv_fit(raw, 720, 130); cv_free(raw); }

	a->bg_base = make_gradient(RGB(10, 16, 40), RGB(3, 5, 14));
	raw = load_png_named(a->assets_dir, "background");
	if (raw) {
		Canvas* bg = cv_cover(raw, UI_W, UI_H);
		cv_free(raw);
		if (bg) { cv_shade(bg, 150); for (int i = 0; i < UI_W * UI_H; i++) bg->px[i] |= 0xFF000000u; cv_free(a->bg_base); a->bg_base = bg; }
	}
	Canvas* battle = load_png_named(a->games_art_dir, "sonicbattle");
	if (battle) {
		a->battle_slot = cv_cover(battle, 560, 315);
		cv_free(battle);
	} else {
		a->battle_slot = make_battle_placeholder(560, 315);
	}

	for (int i = 0; i < NUM_GAMES; i++) {
		/* El fondo difuminado solo conserva 80x45 píxeles de detalle. No hace falta
		 * escalar primero la carátula a 1920x1080 para reducirla inmediatamente:
		 * en PS4 eso dispara uso de CPU/memoria antes del primer fotograma visible. */
		Canvas *art, *tiny;
		debug_log_info("ui_load_assets: preparando caratula %d/%d (%s)", i + 1, NUM_GAMES, GAMES[i].art);
		raw = load_png_named(a->games_art_dir, GAMES[i].art);
		if (raw) {
			art = cv_cover(raw, 560, 315);
			tiny = cv_cover(raw, UI_W / 24, UI_H / 24);
			cv_free(raw);
		} else {
			art = make_placeholder(&GAMES[i], 560, 315);
			tiny = make_placeholder(&GAMES[i], UI_W / 24, UI_H / 24);
		}
		if (!art || !tiny) {
			debug_log_error("ui_load_assets: no se pudo reservar memoria para %s", GAMES[i].art);
			cv_free(art);
			cv_free(tiny);
			continue;
		}
		a->cards_dark[i] = cv_copy(art);
		if (a->cards_dark[i]) {
			cv_shade(a->cards_dark[i], 212);                  /* filtro negro: juego sin ROM */
			cv_round_corners(a->cards_dark[i], 20);
		}
		cv_round_corners(art, 20);
		a->cards[i] = art;
		Canvas* blur = cv_resize(tiny, UI_W, UI_H);
		if (blur) {
			cv_shade(blur, 165);
			for (int p = 0; p < UI_W * UI_H; p++) blur->px[p] |= 0xFF000000u;
		}
		a->backdrops[i] = blur;
		cv_free(tiny);
	}
}

void ui_free_assets(App* a) {
	cv_free(a->icon); cv_free(a->title); cv_free(a->bg_base); cv_free(a->battle_slot);
	for (int i = 0; i < NUM_GAMES; i++) { cv_free(a->cards[i]); cv_free(a->cards_dark[i]); cv_free(a->backdrops[i]); }
}

/* ------------------------------------------------------------------ utilidades */
#define T(c, s, x, y, size, col, fl) gfx_text((c), (s), (x), (y), (size), (col), (fl))
static uint32_t with_alpha(uint32_t c, int a) { return COL_ALPHA(c, a < 0 ? 0 : (a > 255 ? 255 : a)); }

static void panel(Canvas* c, Rect r, uint32_t col, int radius, uint32_t border, int bw) {
	gfx_rrect(c, r, radius, col);
	if (border) gfx_rrect_stroke(c, r, radius, bw, border);
}
static void veil(Canvas* c, int alpha) { gfx_rect(c, rect_make(0, 0, UI_W, UI_H), ARGB(alpha, 0, 0, 0)); }

static void draw_lock(Canvas* c, int cx, int cy, int size) {
	uint32_t col = RGB(225, 230, 245);
	gfx_ring(c, (float) cx, (float) cy - size * 0.12f, size / 3.0f, 5, col);
	gfx_rrect(c, rect_make(cx - size / 2, cy - size / 8, size, (int) (size * 0.75f)), 6, col);
}

/* --------------------------------------------------------------- glifos de ayuda */
enum { GL_DPAD, GL_CROSS, GL_CIRCLE, GL_SQUARE, GL_TRIANGLE, GL_OPTIONS };
typedef struct { int pad; const char* key; const char* label; } Hint;

static int glyph_width(const App* a, const Hint* h) {
	if (a->input_pad) return h->pad == GL_OPTIONS ? 90 : 40;
	int w = gfx_text_width(h->key, 20, TX_BOLD) + 28;
	return w < 40 ? 40 : w;
}
static void draw_glyph(App* a, Canvas* c, const Hint* h, int x, int cy) {
	uint32_t ring = RGB(200, 205, 220), bg = RGB(24, 26, 36);
	if (!a->input_pad) {
		int w = glyph_width(a, h);
		Rect r = rect_make(x, cy - 17, w, 34);
		panel(c, r, RGB(34, 38, 52), 8, ring, 2);
		T(c, h->key, r.x + w / 2, cy, 20, C_SOFT, TX_BOLD | TX_CENTER | TX_VMID);
		return;
	}
	if (h->pad == GL_OPTIONS) {
		Rect r = rect_make(x, cy - 15, 90, 30);
		panel(c, r, bg, 15, ring, 2);
		T(c, "OPTIONS", x + 45, cy, 15, C_SOFT, TX_BOLD | TX_CENTER | TX_VMID);
		return;
	}
	float cx = (float) x + 20, fy = (float) cy;
	gfx_circle(c, cx, fy, 20, bg);
	gfx_ring(c, cx, fy, 20, 2, ring);
	if (h->pad == GL_DPAD) {
		gfx_rrect(c, rect_make((int) cx - 3, cy - 12, 6, 24), 2, C_SOFT);
		gfx_rrect(c, rect_make((int) cx - 12, cy - 3, 24, 6), 2, C_SOFT);
	} else if (h->pad == GL_CROSS) {
		uint32_t col = RGB(110, 165, 255);
		gfx_line(c, cx - 9, fy - 9, cx + 9, fy + 9, 4, col);
		gfx_line(c, cx - 9, fy + 9, cx + 9, fy - 9, 4, col);
	} else if (h->pad == GL_CIRCLE) {
		gfx_ring(c, cx, fy, 10, 3, RGB(255, 105, 115));
	} else if (h->pad == GL_SQUARE) {
		gfx_rrect_stroke(c, rect_make((int) cx - 10, cy - 10, 20, 20), 0, 3, RGB(235, 150, 210));
	} else {
		uint32_t col = RGB(95, 220, 175);
		float p[6] = {cx, fy - 11, cx + 11, fy + 9, cx - 11, fy + 9};
		gfx_line(c, p[0], p[1], p[2], p[3], 3, col);
		gfx_line(c, p[2], p[3], p[4], p[5], 3, col);
		gfx_line(c, p[4], p[5], p[0], p[1], 3, col);
	}
}
static void draw_hints(App* a, Canvas* c, const Hint* items, int n, int right, int cy) {
	int gap = 30, pad = 10, total = 0, widths[6];
	for (int i = 0; i < n; i++) { widths[i] = glyph_width(a, &items[i]) + pad + gfx_text_width(items[i].label, 22, 0); total += widths[i]; }
	total += gap * (n - 1);
	int x = right - total;
	for (int i = 0; i < n; i++) {
		int gw = glyph_width(a, &items[i]);
		draw_glyph(a, c, &items[i], x, cy);
		T(c, items[i].label, x + gw + pad, cy, 22, C_SOFT, TX_VMID);
		x += widths[i] + gap;
	}
}

static void draw_toggle(Canvas* c, Rect rect, int on) {
	Rect r = rect_make(rect.x + rect.w - 24 - 110, rect.y + rect.h / 2 - 22, 110, 44);
	panel(c, r, on ? RGB(60, 200, 120) : RGB(70, 74, 90), 22, 0, 0);
	gfx_circle(c, (float) (on ? r.x + r.w - 24 : r.x + 24), (float) (r.y + 22), 17, C_WHITE);
	T(c, on ? "SÍ" : "NO", on ? r.x + 30 : r.x + r.w - 30, r.y + 22, 20, C_WHITE, TX_BOLD | TX_CENTER | TX_VMID);
}

static void draw_slider_arrow(Canvas* c, int cx, int cy, int dir, uint32_t color) {
	/* Flecha vectorial para que quede nítida a cualquier escala. */
	float tip = (float) cx + dir * 11.0f;
	gfx_line(c, cx - dir * 8.0f, cy, tip, cy, 4.0f, color);
	gfx_line(c, tip, cy, tip - dir * 7.0f, cy - 7.0f, 4.0f, color);
	gfx_line(c, tip, cy, tip - dir * 7.0f, cy + 7.0f, 4.0f, color);
}

static void draw_slider(Canvas* c, Rect rect, int value, uint32_t on_col) {
	int seg_w = 20, gap = 6, h = 30, total = 10 * seg_w + 9 * gap;
	int x0 = rect.x + rect.w - total - 130, y0 = rect.y + rect.h / 2 - h / 2;
	for (int i = 0; i < 10; i++)
		gfx_rrect(c, rect_make(x0 + i * (seg_w + gap), y0, seg_w, h), 4, value >= (i + 1) * 10 ? on_col : RGB(70, 74, 90));
	/* Flecha izquierda = bajar volumen; derecha = subirlo. Las mismas zonas
	 * son táctiles en la pausa de Android. */
	draw_slider_arrow(c, x0 - 24, y0 + h / 2, -1, on_col);
	draw_slider_arrow(c, x0 + total + 24, y0 + h / 2, 1, on_col);
	char buf[16];
	snprintf(buf, sizeof(buf), "%d%%", value);
	T(c, buf, rect.x + rect.w - 24, rect.y + rect.h / 2, 26, on_col == C_DARK ? C_DARK : C_WHITE, TX_BOLD | TX_RIGHT | TX_VMID);
}

/* -------------------------------------------------------------- pantalla principal */
static void draw_header(App* a, Canvas* c) {
	int x = 70;
	if (a->icon) { gfx_blit(c, a->icon, x, 40, 255); x += a->icon->w + 26; }
	if (a->title) gfx_blit(c, a->title, x, 35, 255);
	else {
		T(c, APP_TITLE, x, 36, 66, C_WHITE, TX_BOLD);
		T(c, "LAUNCHER", x + 4, 112, 28, C_GOLD, TX_BOLD);
	}
	char v[64];
	snprintf(v, sizeof(v), "Launcher v%s", APP_VERSION);
	T(c, v, UI_W - 70, 52, 24, C_SOFT, TX_RIGHT);
	T(c, "Emulación por mGBA", UI_W - 70, 88, 22, C_MUTED, TX_RIGHT);
}

static void draw_cards(App* a, Canvas* c) {
	float pulse = 0.5f + 0.5f * sinf((float) a->now * 4.0f);
	int order[NUM_GAMES] = {0, 1, 2};
	for (int i = 0; i < NUM_GAMES; i++)
		for (int j = i + 1; j < NUM_GAMES; j++)
			if (a->anim[order[j]] < a->anim[order[i]]) { int t = order[i]; order[i] = order[j]; order[j] = t; }
	for (int k = 0; k < NUM_GAMES; k++) {
		int i = order[k];
		float an = a->anim[i] < 0 ? 0 : (a->anim[i] > 1 ? 1 : a->anim[i]);
		const GameInfo* g = &GAMES[i];
		int has = a->rom_found[i];
		Canvas* base = has ? a->cards[i] : a->cards_dark[i];
		float scale = 1.0f + 0.07f * an;
		Rect cr = ui_card_rect(a, i);
		int w = (int) (base->w * scale), h = (int) (base->h * scale);
		Rect r = rect_make(cr.x + (cr.w - w) / 2, cr.y + (cr.h - h) / 2 - (int) (12 * an), w, h);
		gfx_rrect(c, rect_move(rect_inflate(r, 10, 10), 0, 14), 26, ARGB((int) (110 + 60 * an), 0, 0, 0));
		if (an > 0.05f) gfx_rrect(c, rect_inflate(r, 26, 26), 32, with_alpha(g->accent, (int) ((60 + 70 * pulse) * an)));
		gfx_blit_scaled(c, base, r, 255);
		if (has && an < 0.99f) gfx_rrect(c, r, 20, ARGB((int) (95 * (1.0f - an)), 0, 0, 0));
		if (!has) {
			draw_lock(c, r.x + r.w / 2, r.y + r.h / 2 - 22, 34);
			T(c, "ROM NO ENCONTRADA", r.x + r.w / 2, r.y + r.h / 2 + 28, 26, RGB(235, 238, 250), TX_BOLD | TX_CENTER | TX_VMID);
			T(c, g->rom, r.x + r.w / 2, r.y + r.h / 2 + 62, 20, C_MUTED, TX_CENTER | TX_VMID);
		}
		if (an > 0.05f) gfx_rrect_stroke(c, rect_inflate(r, 10, 10), 24, 5, with_alpha(g->accent, (int) (255 * an)));
	}
}

static int wrap_lines(const char* text, int size, int maxw, char lines[][256], int maxlines) {
	int n = 0;
	char cur[256] = "";
	const char* p = text;
	while (*p && n < maxlines) {
		while (*p == ' ') p++;
		const char* e = p;
		while (*e && *e != ' ') e++;
		char word[128];
		int wl = (int) (e - p);
		if (wl > 127) wl = 127;
		memcpy(word, p, (size_t) wl);
		word[wl] = 0;
		char test[400];
		snprintf(test, sizeof(test), "%s%s%s", cur, cur[0] ? " " : "", word);
		if (gfx_text_width(test, size, 0) <= maxw || !cur[0]) snprintf(cur, sizeof(cur), "%.250s", test);
		else { snprintf(lines[n++], 256, "%s", cur); snprintf(cur, sizeof(cur), "%s", word); }
		p = e;
	}
	if (cur[0] && n < maxlines) snprintf(lines[n++], 256, "%s", cur);
	return n;
}

static void draw_battle_slot(App* a, Canvas* c) {
	Rect r = ui_battle_slot_rect();
	panel(c, r, ARGB(225, 7, 10, 24), 25, ARGB(100, 255, 181, 46), 2);
	Rect art = rect_make(r.x + 16, r.y + 15, r.w - 32, 286);
	gfx_rrect(c, rect_inflate(art, 5, 5), 16, ARGB(210, 0, 0, 0));
	if (a->battle_slot) gfx_blit_scaled(c, a->battle_slot, art, 255);
	else {
		panel(c, art, ARGB(255, 12, 22, 48), 14, ARGB(170, 80, 150, 255), 2);
		T(c, "SONIC BATTLE", art.x + art.w / 2, art.y + art.h / 2, 34, C_WHITE, TX_BOLD | TX_CENTER | TX_VMID);
	}
	Rect foot = rect_make(r.x + 15, r.y + 307, r.w - 30, 47);
	panel(c, foot, ARGB(235, 13, 17, 32), 14, ARGB(90, 255, 255, 255), 1);
	T(c, "SONIC BATTLE  ·  ESPACIO RESERVADO",
	  foot.x + foot.w / 2, foot.y + foot.h / 2, 20, RGB(255, 205, 75), TX_BOLD | TX_CENTER | TX_VMID);
}

static void draw_detail(App* a, Canvas* c) {
	const GameInfo* g = &GAMES[a->sel];
	int has = a->rom_found[a->sel];
	/* La ficha se reduce al área azul de la referencia; el tercio inferior izquierdo
	 * queda reservado para una futura entrada de Sonic Battle. */
	Rect box = rect_make(625, 555, UI_W - 685, 370);
	panel(c, box, ARGB(222, 8, 10, 22), 26, ARGB(55, 255, 255, 255), 2);
	gfx_rrect(c, rect_make(box.x + 23, box.y + 25, 7, 68), 4, g->accent);
	T(c, g->title, box.x + 43, box.y + 15, 50, C_WHITE, TX_BOLD);
	T(c, g->tagline, box.x + 43, box.y + 78, 23, g->accent, TX_BOLD);

	char lines[7][256];
	int n = wrap_lines(g->summary, 21, 535, lines, 6);
	for (int i = 0; i < n; i++)
		T(c, lines[i], box.x + 38, box.y + 128 + i * 32, 21, C_SOFT, 0);

	char st[96];
	snprintf(st, sizeof(st), "Falta ROM");
	const char* labels[7] = {"ESTADO", "AÑO", "DESARROLLO", "EDITOR", "GÉNERO", "PLATAFORMA", "ARCHIVO ROM"};
	const char* vals[7] = {has ? "Lista para jugar" : st, g->year, g->dev, g->pub, g->genre, "Game Boy Advance", g->rom};
	int x0 = box.x + 635;
	gfx_rect(c, rect_make(x0 - 28, box.y + 25, 2, box.h - 50), ARGB(55, 255, 255, 255));
	for (int i = 0; i < 7; i++) {
		int y = box.y + 22 + i * 45;
		T(c, labels[i], x0, y + 5, 18, C_MUTED, TX_BOLD);
		T(c, vals[i], x0 + 154, y, 22, i == 0 ? (has ? C_GREEN : C_RED) : C_WHITE, i == 0 ? TX_BOLD : 0);
	}
}

static void draw_buttons(App* a, Canvas* c) {
	uint32_t acc = GAMES[a->sel].accent;
	int has = a->rom_found[a->sel];
	static const char* labels[4] = {"JUGAR", "CARPETA DE ROMS", "AJUSTES", "SALIR"};
	for (int j = 0; j < 4; j++) {
		Rect r = ui_btn_rect(j);
		int focused = (a->zone == 1 && a->btn == j);
		const char* label = labels[j];
		int enabled = 1;
		if (j == 0 && !has) { enabled = 0; label = "ROM NO ENCONTRADA"; }
		if (!enabled) {
			panel(c, r, ARGB(235, 45, 47, 58), 16, 0, 0);
			T(c, label, r.x + r.w / 2, r.y + r.h / 2, 26, RGB(130, 135, 152), TX_BOLD | TX_CENTER | TX_VMID);
		} else if (focused) {
			gfx_rrect(c, rect_inflate(r, 10, 10), 22, with_alpha(acc, 90));
			panel(c, r, acc, 16, C_WHITE, 3);
			T(c, label, r.x + r.w / 2, r.y + r.h / 2, 28, C_WHITE, TX_BOLD | TX_CENTER | TX_VMID);
		} else {
			panel(c, r, j == 0 ? with_alpha(acc, 70) : ARGB(26, 255, 255, 255), 16, ARGB(70, 255, 255, 255), 2);
			T(c, label, r.x + r.w / 2, r.y + r.h / 2, j == 0 ? 28 : 24, j == 0 ? C_WHITE : C_SOFT, TX_BOLD | TX_CENTER | TX_VMID);
		}
	}
	if (a->zone == 0) {
		Hint h[3] = {{GL_DPAD, "← →", "Elegir"}, {GL_CROSS, "Enter", "Jugar"}, {GL_OPTIONS, "Tab", "Ajustes"}};
		draw_hints(a, c, h, 3, UI_W - 60, 980);
	} else {
		Hint h[3] = {{GL_DPAD, "← →", "Elegir"}, {GL_CROSS, "Enter", "Aceptar"}, {GL_CIRCLE, "Esc", "Volver"}};
		draw_hints(a, c, h, 3, UI_W - 60, 980);
	}
}

/* ------------------------------------------------------------------- ajustes */
static void draw_settings(App* a, Canvas* c) {
	veil(c, 175);
	Rect p = settings_panel();
	uint32_t acc = GAMES[a->sel].accent;
	panel(c, p, ARGB(250, 14, 17, 32), 28, ARGB(60, 255, 255, 255), 2);
	T(c, "AJUSTES", p.x + 50, p.y + 34, 52, C_WHITE, TX_BOLD);
	for (int k = 0; k < SET_COUNT; k++) {
		Rect r = ui_settings_row(k);
		int focused = k == a->set_sel;
		if (focused) panel(c, r, with_alpha(acc, 95), 16, acc, 3); else panel(c, r, ARGB(18, 255, 255, 255), 16, 0, 0);
		T(c, SETTINGS[k].label, r.x + 28, r.y + r.h / 2, 28, focused ? C_WHITE : C_SOFT, TX_VMID | (focused ? TX_BOLD : 0));
		if (SETTINGS[k].kind == 0) draw_toggle(c, r, *cfg_field(&a->cfg, SETTINGS[k].field));
		else if (SETTINGS[k].kind == 1) draw_slider(c, r, a->cfg.volume, C_GOLD);
	}
	char buf[96];
	snprintf(buf, sizeof(buf), "Motor: mGBA integrado (%s)", emu_version());
	T(c, buf, p.x + 50, p.y + p.h - 120, 22, C_GREEN, TX_VMID);
	snprintf(buf, sizeof(buf), "Audio: %s", a->host.audio_info && a->host.audio_info[0] ? a->host.audio_info : (a->host.has_audio ? "activo" : "sin dispositivo"));
	T(c, buf, p.x + 50, p.y + p.h - 92, 22, a->host.has_audio ? C_GREEN : C_RED, TX_VMID);
	snprintf(buf, sizeof(buf), "Vídeo: %s", a->host.video_info && a->host.video_info[0] ? a->host.video_info : "?");
	T(c, buf, p.x + 50, p.y + p.h - 64, 22, C_GREEN, TX_VMID);
	Hint h[3] = {{GL_DPAD, "↑ ↓", "Mover"}, {GL_CROSS, "Enter", "Cambiar"}, {GL_CIRCLE, "Esc", "Volver"}};
	draw_hints(a, c, h, 3, p.x + p.w - 30, p.y + p.h - 50);
}

static uint32_t cur_accent(const App* a) { return GAMES[a->emu ? a->game_idx : a->sel].accent; }

static void draw_controls(App* a, Canvas* c) {
	veil(c, 185);
	Rect p = controls_panel();
	uint32_t acc = cur_accent(a);
	panel(c, p, ARGB(250, 14, 17, 32), 28, ARGB(60, 255, 255, 255), 2);
	T(c, "CONTROLES", p.x + 50, p.y + 28, 50, C_WHITE, TX_BOLD);
	Rect r0 = ui_ctrl_row_rect(0);
	T(c, "ACCIÓN", r0.x + 20, p.y + 112, 20, C_MUTED, TX_BOLD);
	T(c, "TECLADO", ui_ctrl_cell_rect(0, 0).x + 18, p.y + 112, 20, C_MUTED, TX_BOLD);
	T(c, a->pad_count >= 1 ? "MANDO J1 · CONECTADO" : "MANDO J1 · SIN DETECTAR",
	  ui_ctrl_cell_rect(0, 1).x + 10, p.y + 112, 17, a->pad_count >= 1 ? C_WHITE : C_MUTED, TX_BOLD);
	T(c, a->pad_count >= 2 ? "MANDO J2 · CONECTADO" : "MANDO J2 · SIN DETECTAR",
	  ui_ctrl_cell_rect(0, 2).x + 10, p.y + 112, 17, a->pad_count >= 2 ? C_WHITE : C_MUTED, TX_BOLD);
	float pulse = 0.5f + 0.5f * sinf((float) a->now * 6.0f);
	for (int k = 0; k < ACT_COUNT + 2; k++) {
		Rect r = ui_ctrl_row_rect(k);
		int focused = k == a->ctrl_row;
		if (k < ACT_COUNT) {
			panel(c, r, focused ? with_alpha(acc, 60) : ARGB(14, 255, 255, 255), 10, 0, 0);
			T(c, action_label[k], r.x + 20, r.y + r.h / 2, 23, focused ? C_WHITE : C_SOFT, TX_VMID | (focused ? TX_BOLD : 0));
			for (int col = 0; col < 3; col++) {
				Rect cell = ui_ctrl_cell_rect(k, col);
				int sel = focused && a->ctrl_col == col;
				int waiting = a->capture_kind == col && a->capture_kind >= 0 && sel;
				int code = col == 0 ? a->cfg.keys[k] : a->cfg.pad[col - 1][k];
				if (waiting) {
					panel(c, cell, ARGB((int) (70 + 70 * pulse), 255, 205, 60), 10, C_GOLD, 3);
					T(c, col == 0 ? "Pulsa una tecla…" : "Pulsa un botón…",
					  cell.x + cell.w / 2, cell.y + cell.h / 2, 20, C_WHITE, TX_BOLD | TX_CENTER | TX_VMID);
				} else {
					panel(c, cell, ARGB(90, 0, 0, 0), 10, sel ? acc : ARGB(40, 255, 255, 255), sel ? 3 : 1);
					char buf[64];
					if (col == 0) key_label(a, code, buf, sizeof(buf)); else pad_label(code, buf, sizeof(buf));
					T(c, buf, cell.x + cell.w / 2, cell.y + cell.h / 2, 21, code >= 0 ? C_WHITE : C_MUTED, TX_CENTER | TX_VMID | (sel ? TX_BOLD : 0));
				}
			}
		} else {
			panel(c, r, focused ? with_alpha(acc, 110) : ARGB(18, 255, 255, 255), 10, focused ? acc : 0, 3);
			T(c, k == ACT_COUNT ? "Restablecer todos los controles por defecto" : "Volver", r.x + r.w / 2, r.y + r.h / 2, 25, C_WHITE, TX_BOLD | TX_CENTER | TX_VMID);
		}
	}
	if (a->capture_kind >= 0) {
		Hint h[2] = {{GL_CIRCLE, "Esc", "Cancelar"}, {GL_SQUARE, "Supr", "Borrar"}};
		draw_hints(a, c, h, 2, p.x + p.w - 40, p.y + p.h - 50);
	} else {
		Hint h[4] = {{GL_DPAD, "↑ ↓ ← →", "Mover"}, {GL_CROSS, "Enter", "Reasignar"}, {GL_SQUARE, "Supr", "Borrar"}, {GL_CIRCLE, "Esc", "Volver"}};
		draw_hints(a, c, h, 4, p.x + p.w - 40, p.y + p.h - 50);
	}
}



static void draw_modal(App* a, Canvas* c) {
	veil(c, 200);
	Rect box = rect_make(UI_W / 2 - 500, UI_H / 2 - 240, 1000, 480);
	panel(c, box, ARGB(252, 14, 17, 32), 28, ARGB(70, 255, 255, 255), 2);
	T(c, a->modal_title, UI_W / 2, box.y + 60, 44, C_WHITE, TX_BOLD | TX_CENTER | TX_VMID);
	char lines[8][256];
	int n = wrap_lines(a->modal_text, 28, box.w - 120, lines, 7);
	for (int i = 0; i < n; i++) T(c, lines[i], UI_W / 2, box.y + 120 + i * 38, 28, C_SOFT, TX_CENTER);
	if (a->modal_kind == 1) {
		Rect yes = ui_modal_yes(), no = ui_modal_no();
		panel(c, yes, a->modal_choice == 0 ? cur_accent(a) : ARGB(255, 28, 32, 54), 16,
		      a->modal_choice == 0 ? C_WHITE : ARGB(100, 255, 255, 255), a->modal_choice == 0 ? 4 : 2);
		panel(c, no, a->modal_choice == 1 ? cur_accent(a) : ARGB(255, 28, 32, 54), 16,
		      a->modal_choice == 1 ? C_WHITE : ARGB(100, 255, 255, 255), a->modal_choice == 1 ? 4 : 2);
		T(c, "1J", yes.x + yes.w / 2, yes.y + yes.h / 2, 34, C_WHITE, TX_BOLD | TX_CENTER | TX_VMID);
		T(c, "2J", no.x + no.w / 2, no.y + no.h / 2, 34, C_WHITE, TX_BOLD | TX_CENTER | TX_VMID);
	} else {
		Rect ok = ui_modal_ok();
		panel(c, ok, cur_accent(a), 16, C_WHITE, 3);
		T(c, "ACEPTAR", ok.x + ok.w / 2, ok.y + ok.h / 2, 28, C_WHITE, TX_BOLD | TX_CENTER | TX_VMID);
	}
}

/* --------------------------------------------------- pausa (estilo Sonic Mania) y ranuras */
static void draw_pause(App* a, Canvas* c) {
	uint32_t acc = cur_accent(a);
	veil(c, 150);
	float band[8] = {0, 0, 1010, 0, 770, (float) UI_H, 0, (float) UI_H};
	gfx_poly(c, band, 4, ARGB(215, 8, 12, 34));
	gfx_line(c, 1010, 0, 770, (float) UI_H, 10, acc);
	gfx_line(c, 1030, 0, 790, (float) UI_H, 3, ARGB(200, 255, 255, 255));
	T(c, "PAUSA", 110, 36, 118, C_WHITE, TX_BOLD | TX_ITALIC);
	char up[96];
	snprintf(up, sizeof(up), "%s", GAMES[a->game_idx].title);
	for (char* p = up; *p; p++) if (*p >= 'a' && *p <= 'z') *p -= 32;
	T(c, up, 116, 160, 30, acc, TX_BOLD | TX_ITALIC);

	for (int i = 0; i < PAUSE_COUNT; i++) {
		Rect r = ui_pause_rect(i);
		float an = a->pause_anim[i] < 0 ? 0 : (a->pause_anim[i] > 1 ? 1 : a->pause_anim[i]);
		int skew = 28;
		if (an > 0.02f) {
			float bw = r.w * (0.30f + 0.70f * an);
			float pts[8] = {r.x + skew, (float) r.y, r.x + bw + skew, (float) r.y, r.x + bw, (float) r.y + r.h, (float) r.x, (float) r.y + r.h};
			gfx_poly(c, pts, 4, C_GOLD);
			gfx_line(c, r.x + skew + 6, r.y + 8, r.x + bw + skew - 18, r.y + 8, 3, ARGB(130, 255, 235, 140));
		}
		int sel = i == a->pause_sel;
		uint32_t col = sel ? C_DARK : C_WHITE;
		T(c, PAUSE_LABELS[i], r.x + 58, r.y + r.h / 2, 40, col, TX_BOLD | TX_ITALIC | TX_VMID);
		Rect right = rect_make(r.x, r.y, r.w + skew - 10, r.h);
		if (i == 3) draw_slider(c, right, a->cfg.volume, sel ? C_DARK : C_GOLD);
		else if (i == 4) T(c, a->cfg.fullscreen ? "SÍ" : "NO", right.x + right.w - 24, r.y + r.h / 2, 36, col, TX_BOLD | TX_ITALIC | TX_RIGHT | TX_VMID);
		if (sel && (i == 3 || i == 4)) T(c, "<  >", r.x + 58, r.y + r.h - 10, 20, C_DARK, TX_BOLD | TX_VMID);
	}
	Hint h[3] = {{GL_DPAD, "↑ ↓", "Mover"}, {GL_CROSS, "Enter", "Aceptar"}, {GL_CIRCLE, "Esc", "Continuar"}};
	draw_hints(a, c, h, 3, UI_W - 60, UI_H - 50);
}

static void draw_slots(App* a, Canvas* c) {
	uint32_t acc = cur_accent(a);
	veil(c, 205);
	T(c, a->slot_mode == 0 ? "GUARDAR PARTIDA" : "CARGAR PARTIDA", 100, 40, 84, C_WHITE, TX_BOLD | TX_ITALIC);
	char up[96];
	snprintf(up, sizeof(up), "%s", GAMES[a->game_idx].title);
	for (char* p = up; *p; p++) if (*p >= 'a' && *p <= 'z') *p -= 32;
	T(c, up, 104, 138, 28, acc, TX_BOLD | TX_ITALIC);
	float pulse = 0.5f + 0.5f * sinf((float) a->now * 5.0f);
	for (int n = 0; n < SLOT_COUNT; n++) {
		Rect r = ui_slot_rect(n);
		int num = n + 1, exists = slot_exists(a, num), sel = n == a->slot_sel;
		int dim = a->slot_mode == 1 && !exists;
		uint32_t fill = dim ? ARGB(150, 10, 14, 38) : ARGB(235, 10, 14, 38);
		if (sel) gfx_rrect(c, rect_inflate(r, 18, 18), 24, with_alpha(acc, (int) (40 + 50 * pulse)));
		panel(c, r, fill, 18, sel ? acc : ARGB(50, 255, 255, 255), sel ? 5 : 2);
		Rect th = rect_make(r.x + 20, r.y + 20, 360, 240);
		if (exists && a->slot_thumbs[num]) gfx_blit(c, a->slot_thumbs[num], th.x, th.y, 255);
		else {
			panel(c, th, ARGB(160, 0, 0, 0), 12, ARGB(30, 255, 255, 255), 1);
			T(c, exists ? "PARTIDA GUARDADA" : "VACÍA", th.x + th.w / 2, th.y + th.h / 2, 28, exists ? C_SOFT : C_MUTED, TX_BOLD | TX_ITALIC | TX_CENTER | TX_VMID);
		}
		char buf[64];
		snprintf(buf, sizeof(buf), "RANURA %d", num);
		T(c, buf, r.x + 24, r.y + 274, 34, sel ? C_GOLD : C_WHITE, TX_BOLD | TX_ITALIC);
		if (exists) {
			char p[PATH_MAX_APP + 16], when[64];
			slot_path(a, num, p, sizeof(p));
			sys_format_time(sys_mtime(p), when, sizeof(when));
			T(c, when, r.x + 24, r.y + 318, 22, C_SOFT, 0);
			if (a->delete_slot == num && a->now - a->delete_t < 3.0) T(c, "¿BORRAR?", r.x + r.w - 24, r.y + 318, 24, C_RED, TX_BOLD | TX_RIGHT);
		}
	}
	Hint h[4] = {{GL_DPAD, "↑ ↓ ← →", "Elegir"}, {GL_CROSS, "Enter", a->slot_mode == 0 ? "Guardar" : "Cargar"}, {GL_SQUARE, "Supr", "Borrar"}, {GL_CIRCLE, "Esc", "Volver"}};
	draw_hints(a, c, h, 4, UI_W - 60, UI_H - 40);
}

/* ------------------------------------------------------------- HUD durante el juego */
static void draw_hud(App* a, Canvas* c) {
	int y = 22;
	if (a->cfg.show_fps) {
		char b[32];
		snprintf(b, sizeof(b), "FPS %.0f", a->fps);
		T(c, b, 34, y + 2, 30, ARGB(200, 0, 0, 0), TX_BOLD);
		T(c, b, 32, y, 30, C_WHITE, TX_BOLD);
		y += 38;
	}
	if (a->turbo && a->state == ST_GAME) {
		char b[16];
		snprintf(b, sizeof(b), ">> x%d", TURBO_FRAMES);
		T(c, b, 34, y + 2, 30, ARGB(200, 0, 0, 0), TX_BOLD);
		T(c, b, 32, y, 30, C_GOLD, TX_BOLD);
	}
}

static void draw_toast(App* a, Canvas* c, int y) {
	if (!a->toast[0] || a->now >= a->toast_until) return;
	int w = gfx_text_width(a->toast, 26, 0) + 70;
	Rect r = rect_make(UI_W / 2 - w / 2, y - 27, w, 54);
	panel(c, r, ARGB(235, 20, 22, 34), 27, ARGB(80, 255, 255, 255), 2);
	T(c, a->toast, UI_W / 2, y, 26, C_WHITE, TX_CENTER | TX_VMID);
}

/* ---------------------------------------------------------------- composición */
void ui_draw(App* a) {
	Canvas* c = a->ui;
	if (a->anim[a->sel] > 0.985f) memcpy(c->px, a->backdrops[a->sel]->px, (size_t) UI_W * UI_H * 4);
	else {
		memcpy(c->px, a->bg_base->px, (size_t) UI_W * UI_H * 4);
		for (int i = 0; i < NUM_GAMES; i++)
			if (a->anim[i] > 0.02f) gfx_blit(c, a->backdrops[i], 0, 0, (int) (255 * (a->anim[i] > 1 ? 1 : a->anim[i])));
	}
	draw_header(a, c);
	draw_cards(a, c);
	draw_battle_slot(a, c);
	draw_detail(a, c);
	draw_buttons(a, c);
	draw_toast(a, c, 1050);
	if (a->state == ST_SETTINGS) { if (a->settings_page) draw_controls(a, c); else draw_settings(a, c); }
	if (a->modal) draw_modal(a, c);
	if (a->loading) {
		veil(c, 190);
		char b[128];
		snprintf(b, sizeof(b), "Iniciando %s…", GAMES[a->pending_launch >= 0 ? a->pending_launch : a->sel].title);
		T(c, b, UI_W / 2, UI_H / 2, 52, C_WHITE, TX_BOLD | TX_CENTER | TX_VMID);
	}
}

void app_render(App* a, AppFrame* out) {
	memset(out, 0, sizeof(*out));
	if (!a->emu) {
		ui_draw(a);
		out->ui = a->ui;
		return;
	}
	out->in_game = 1;
	out->game = a->have_frame ? a->frame : NULL;
	out->game2 = (a->have_frame && a->emu2) ? a->frame2 : NULL;
	out->multiplayer = a->emu2 != NULL;
	out->smooth = a->cfg.smooth;
	out->integer_scale = a->cfg.integer_scale;
	Canvas* c = a->ui;
	int toast_on = a->toast[0] && a->now < a->toast_until;
	if (a->state == ST_GAME) {
		if (!(a->cfg.show_fps || a->turbo || toast_on)) { out->ui = NULL; return; }
		cv_clear(c, 0);
		draw_hud(a, c);
		draw_toast(a, c, 1020);
		out->ui = c;
		return;
	}
	cv_clear(c, 0);
	if (a->state == ST_PAUSE) draw_pause(a, c);
	else if (a->state == ST_SLOTS) draw_slots(a, c);
	else if (a->state == ST_SETTINGS) draw_controls(a, c);
	if (a->modal) draw_modal(a, c);
	draw_toast(a, c, 62);
	out->ui = c;
}
