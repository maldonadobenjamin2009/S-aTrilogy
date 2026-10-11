/* png.c - decodificador/codificador PNG mínimo y sin dependencias (inflate propio, basado en la
 * técnica de "puff"). Solo PNG no entrelazado; es suficiente para las imágenes del launcher. */
#include "png.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------- inflate */
#define MAXBITS 15
typedef struct {
	const uint8_t* in; size_t inlen, inpos;
	uint32_t bitbuf; int bitcnt;
	uint8_t* out; size_t outlen, outcap;
	int err;
} Inf;

typedef struct { short count[MAXBITS + 1]; short symbol[288]; } Huff;

static int getbits(Inf* s, int need) {
	uint32_t val = s->bitbuf;
	while (s->bitcnt < need) {
		if (s->inpos >= s->inlen) { s->err = 1; return 0; }
		val |= (uint32_t) s->in[s->inpos++] << s->bitcnt;
		s->bitcnt += 8;
	}
	s->bitbuf = val >> need;
	s->bitcnt -= need;
	return (int) (val & ((1u << need) - 1));
}

static int construct(Huff* h, const short* length, int n) {
	short offs[MAXBITS + 1];
	int symbol, len, left;
	for (len = 0; len <= MAXBITS; len++) h->count[len] = 0;
	for (symbol = 0; symbol < n; symbol++) h->count[length[symbol]]++;
	if (h->count[0] == n) return 0;
	left = 1;
	for (len = 1; len <= MAXBITS; len++) {
		left <<= 1;
		left -= h->count[len];
		if (left < 0) return left;
	}
	offs[1] = 0;
	for (len = 1; len < MAXBITS; len++) offs[len + 1] = offs[len] + h->count[len];
	for (symbol = 0; symbol < n; symbol++)
		if (length[symbol] != 0) h->symbol[offs[length[symbol]]++] = (short) symbol;
	return left;
}

static int decode(Inf* s, const Huff* h) {
	int len, code = 0, first = 0, index = 0;
	for (len = 1; len <= MAXBITS; len++) {
		code |= getbits(s, 1);
		if (s->err) return -1;
		int count = h->count[len];
		if (code - count < first) return h->symbol[index + (code - first)];
		index += count;
		first += count;
		first <<= 1;
		code <<= 1;
	}
	return -1;
}

static void put(Inf* s, uint8_t b) {
	if (s->outlen == s->outcap) {
		size_t nc = s->outcap ? s->outcap * 2 : 65536;
		uint8_t* p = realloc(s->out, nc);
		if (!p) { s->err = 1; return; }
		s->out = p; s->outcap = nc;
	}
	s->out[s->outlen++] = b;
}

static const short LENS[29] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
static const short LEXT[29] = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
static const short DISTS[30] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
static const short DEXT[30] = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};

static int codes(Inf* s, const Huff* lencode, const Huff* distcode) {
	for (;;) {
		int sym = decode(s, lencode);
		if (sym < 0 || s->err) return -1;
		if (sym < 256) {
			put(s, (uint8_t) sym);
		} else if (sym == 256) {
			return 0;
		} else {
			sym -= 257;
			if (sym >= 29) return -1;
			int len = LENS[sym] + getbits(s, LEXT[sym]);
			int ds = decode(s, distcode);
			if (ds < 0 || ds >= 30) return -1;
			size_t dist = (size_t) DISTS[ds] + (size_t) getbits(s, DEXT[ds]);
			if (s->err || dist > s->outlen) return -1;
			while (len--) put(s, s->out[s->outlen - dist]);
		}
		if (s->err) return -1;
	}
}

static int fixed_tables(Huff* lencode, Huff* distcode) {
	short lengths[288];
	int i;
	for (i = 0; i < 144; i++) lengths[i] = 8;
	for (; i < 256; i++) lengths[i] = 9;
	for (; i < 280; i++) lengths[i] = 7;
	for (; i < 288; i++) lengths[i] = 8;
	construct(lencode, lengths, 288);
	for (i = 0; i < 30; i++) lengths[i] = 5;
	construct(distcode, lengths, 30);
	return 0;
}

static int dynamic(Inf* s) {
	static const short order[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
	short lengths[320];
	Huff lencode, distcode;
	int nlen = getbits(s, 5) + 257, ndist = getbits(s, 5) + 1, ncode = getbits(s, 4) + 4, i;
	if (nlen > 286 || ndist > 30) return -1;
	for (i = 0; i < ncode; i++) lengths[order[i]] = (short) getbits(s, 3);
	for (; i < 19; i++) lengths[order[i]] = 0;
	if (construct(&lencode, lengths, 19) != 0) return -1;
	i = 0;
	while (i < nlen + ndist) {
		int sym = decode(s, &lencode);
		if (sym < 0 || s->err) return -1;
		if (sym < 16) {
			lengths[i++] = (short) sym;
		} else {
			int len = 0, rep;
			if (sym == 16) {
				if (i == 0) return -1;
				len = lengths[i - 1];
				rep = 3 + getbits(s, 2);
			} else if (sym == 17) {
				rep = 3 + getbits(s, 3);
			} else {
				rep = 11 + getbits(s, 7);
			}
			if (i + rep > nlen + ndist) return -1;
			while (rep--) lengths[i++] = (short) len;
		}
	}
	if (lengths[256] == 0) return -1;
	if (construct(&lencode, lengths, nlen) < 0) return -1;
	if (construct(&distcode, lengths + nlen, ndist) < 0) return -1;
	return codes(s, &lencode, &distcode);
}

/* zlib: salta cabecera de 2 bytes. Devuelve buffer de malloc. */
static uint8_t* zlib_inflate(const uint8_t* in, size_t inlen, size_t* outlen) {
	Inf s;
	memset(&s, 0, sizeof(s));
	if (inlen < 2) return NULL;
	s.in = in + 2; s.inlen = inlen - 2;
	int last;
	do {
		last = getbits(&s, 1);
		int type = getbits(&s, 2);
		if (s.err) break;
		if (type == 0) {
			s.bitbuf = 0; s.bitcnt = 0;
			if (s.inpos + 4 > s.inlen) { s.err = 1; break; }
			unsigned len = s.in[s.inpos] | (s.in[s.inpos + 1] << 8);
			s.inpos += 4;
			if (s.inpos + len > s.inlen) { s.err = 1; break; }
			while (len--) put(&s, s.in[s.inpos++]);
		} else if (type == 1) {
			Huff l, d;
			fixed_tables(&l, &d);
			if (codes(&s, &l, &d) != 0) s.err = 1;
		} else if (type == 2) {
			if (dynamic(&s) != 0) s.err = 1;
		} else {
			s.err = 1;
		}
	} while (!last && !s.err);
	if (s.err) { free(s.out); return NULL; }
	*outlen = s.outlen;
	return s.out;
}

/* ------------------------------------------------------------ decodificar */
static uint32_t be32(const uint8_t* p) { return ((uint32_t) p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }

static int paeth(int a, int b, int c) {
	int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
	return (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
}

uint32_t* png_decode(const uint8_t* d, size_t len, int* ow, int* oh) {
	static const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
	if (len < 33 || memcmp(d, sig, 8) != 0) return NULL;
	size_t pos = 8;
	int w = 0, h = 0, depth = 0, ctype = 0, interlace = 0;
	uint8_t pal[256][4];
	int npal = 0;
	for (int i = 0; i < 256; i++) { pal[i][0] = pal[i][1] = pal[i][2] = 0; pal[i][3] = 255; }
	uint8_t* idat = NULL;
	size_t idat_len = 0;
	int trns_gray = -1;
	while (pos + 12 <= len) {
		uint32_t clen = be32(d + pos);
		const uint8_t* type = d + pos + 4;
		const uint8_t* body = d + pos + 8;
		if (pos + 12 + clen > len) break;
		if (!memcmp(type, "IHDR", 4) && clen >= 13) {
			w = (int) be32(body); h = (int) be32(body + 4);
			depth = body[8]; ctype = body[9]; interlace = body[12];
		} else if (!memcmp(type, "PLTE", 4)) {
			npal = (int) (clen / 3);
			if (npal > 256) npal = 256;
			for (int i = 0; i < npal; i++) { pal[i][0] = body[i * 3]; pal[i][1] = body[i * 3 + 1]; pal[i][2] = body[i * 3 + 2]; }
		} else if (!memcmp(type, "tRNS", 4)) {
			if (ctype == 3) { for (uint32_t i = 0; i < clen && i < 256; i++) pal[i][3] = body[i]; }
			else if (ctype == 0 && clen >= 2) trns_gray = (body[0] << 8) | body[1];
		} else if (!memcmp(type, "IDAT", 4)) {
			uint8_t* p = realloc(idat, idat_len + clen);
			if (!p) { free(idat); return NULL; }
			idat = p;
			memcpy(idat + idat_len, body, clen);
			idat_len += clen;
		} else if (!memcmp(type, "IEND", 4)) {
			break;
		}
		pos += 12 + clen;
	}
	if (w <= 0 || h <= 0 || w > 16384 || h > 16384 || interlace != 0 || !idat) { free(idat); return NULL; }
	int channels = (ctype == 0) ? 1 : (ctype == 2) ? 3 : (ctype == 3) ? 1 : (ctype == 4) ? 2 : (ctype == 6) ? 4 : 0;
	if (!channels || (depth != 8 && depth != 16 && ctype != 0 && ctype != 3)) { free(idat); return NULL; }
	size_t rawlen;
	uint8_t* raw = zlib_inflate(idat, idat_len, &rawlen);
	free(idat);
	if (!raw) return NULL;
	int bpp_bits = channels * depth;
	size_t stride = ((size_t) w * bpp_bits + 7) / 8;
	int bpp = (bpp_bits + 7) / 8;
	if (rawlen < (stride + 1) * (size_t) h) { free(raw); return NULL; }
	uint32_t* out = malloc((size_t) w * h * 4);
	uint8_t* prev = calloc(stride, 1);
	uint8_t* cur = malloc(stride);
	if (!out || !prev || !cur) { free(raw); free(out); free(prev); free(cur); return NULL; }
	for (int y = 0; y < h; y++) {
		const uint8_t* line = raw + (size_t) y * (stride + 1);
		int ft = line[0];
		memcpy(cur, line + 1, stride);
		for (size_t i = 0; i < stride; i++) {
			int a = (i >= (size_t) bpp) ? cur[i - bpp] : 0;
			int b = prev[i];
			int c = (i >= (size_t) bpp) ? prev[i - bpp] : 0;
			switch (ft) {
			case 1: cur[i] = (uint8_t) (cur[i] + a); break;
			case 2: cur[i] = (uint8_t) (cur[i] + b); break;
			case 3: cur[i] = (uint8_t) (cur[i] + ((a + b) >> 1)); break;
			case 4: cur[i] = (uint8_t) (cur[i] + paeth(a, b, c)); break;
			default: break;
			}
		}
		for (int x = 0; x < w; x++) {
			unsigned r, g, bl, a = 255;
			if (depth == 8 || depth == 16) {
				int step = depth / 8;
				const uint8_t* p = cur + (size_t) x * channels * step;
				switch (ctype) {
				case 0: r = g = bl = p[0]; if (trns_gray >= 0 && depth == 8 && p[0] == (trns_gray & 255)) a = 0; break;
				case 2: r = p[0]; g = p[step]; bl = p[2 * step]; break;
				case 3: { unsigned i = p[0]; r = pal[i][0]; g = pal[i][1]; bl = pal[i][2]; a = pal[i][3]; break; }
				case 4: r = g = bl = p[0]; a = p[step]; break;
				default: r = p[0]; g = p[step]; bl = p[2 * step]; a = p[3 * step]; break;
				}
			} else { /* 1, 2 o 4 bits: gris o paleta */
				size_t bit = (size_t) x * depth;
				unsigned v = (cur[bit >> 3] >> (8 - depth - (bit & 7))) & ((1u << depth) - 1);
				if (ctype == 3) { r = pal[v][0]; g = pal[v][1]; bl = pal[v][2]; a = pal[v][3]; }
				else { r = g = bl = v * 255 / ((1u << depth) - 1); }
			}
			out[(size_t) y * w + x] = (a << 24) | (r << 16) | (g << 8) | bl;
		}
		uint8_t* t = prev; prev = cur; cur = t;
	}
	free(raw); free(prev); free(cur);
	*ow = w; *oh = h;
	return out;
}

uint32_t* png_load_file(const char* path, int* w, int* h) {
	FILE* f = fopen(path, "rb");
	if (!f) return NULL;
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (n <= 0 || n > 64 * 1024 * 1024) { fclose(f); return NULL; }
	uint8_t* buf = malloc((size_t) n);
	if (!buf || fread(buf, 1, (size_t) n, f) != (size_t) n) { free(buf); fclose(f); return NULL; }
	fclose(f);
	uint32_t* px = png_decode(buf, (size_t) n, w, h);
	free(buf);
	return px;
}

/* ------------------------------------------------------------- codificar */
static uint32_t crc_table[256];
static void crc_init(void) {
	if (crc_table[1]) return;
	for (uint32_t n = 0; n < 256; n++) {
		uint32_t c = n;
		for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
		crc_table[n] = c;
	}
}
static uint32_t crc_update(uint32_t crc, const uint8_t* p, size_t n) {
	for (size_t i = 0; i < n; i++) crc = crc_table[(crc ^ p[i]) & 255] ^ (crc >> 8);
	return crc;
}
static void put32(FILE* f, uint32_t v) { uint8_t b[4] = {v >> 24, v >> 16, v >> 8, v}; fwrite(b, 1, 4, f); }
static void chunk(FILE* f, const char* type, const uint8_t* data, uint32_t n) {
	put32(f, n);
	fwrite(type, 1, 4, f);
	if (n) fwrite(data, 1, n, f);
	uint32_t c = crc_update(0xFFFFFFFFu, (const uint8_t*) type, 4);
	if (n) c = crc_update(c, data, n);
	put32(f, c ^ 0xFFFFFFFFu);
}

int png_save_file(const char* path, const uint32_t* px, int w, int h, int stride_px, int alpha) {
	crc_init();
	int ch = alpha ? 4 : 3;
	size_t rawlen = (size_t) h * (1 + (size_t) w * ch);
	uint8_t* raw = malloc(rawlen);
	if (!raw) return 0;
	size_t o = 0;
	for (int y = 0; y < h; y++) {
		raw[o++] = 0;
		for (int x = 0; x < w; x++) {
			uint32_t v = px[(size_t) y * stride_px + x];
			raw[o++] = (uint8_t) (v >> 16); raw[o++] = (uint8_t) (v >> 8); raw[o++] = (uint8_t) v;
			if (alpha) raw[o++] = (uint8_t) (v >> 24);
		}
	}
	size_t nblocks = (rawlen + 65534) / 65535;
	size_t zlen = 2 + rawlen + nblocks * 5 + 4;
	uint8_t* z = malloc(zlen);
	if (!z) { free(raw); return 0; }
	size_t zp = 0;
	z[zp++] = 0x78; z[zp++] = 0x01;
	uint32_t a = 1, b = 0;
	for (size_t i = 0; i < rawlen; i++) { a = (a + raw[i]) % 65521; b = (b + a) % 65521; }
	for (size_t off = 0; off < rawlen; off += 65535) {
		size_t n = rawlen - off > 65535 ? 65535 : rawlen - off;
		z[zp++] = (off + n >= rawlen) ? 1 : 0;
		z[zp++] = (uint8_t) n; z[zp++] = (uint8_t) (n >> 8);
		z[zp++] = (uint8_t) ~n; z[zp++] = (uint8_t) (~n >> 8);
		memcpy(z + zp, raw + off, n);
		zp += n;
	}
	uint32_t ad = (b << 16) | a;
	z[zp++] = ad >> 24; z[zp++] = ad >> 16; z[zp++] = ad >> 8; z[zp++] = ad;
	free(raw);
	FILE* f = fopen(path, "wb");
	if (!f) { free(z); return 0; }
	static const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
	fwrite(sig, 1, 8, f);
	uint8_t ihdr[13] = {w >> 24, w >> 16, w >> 8, w, h >> 24, h >> 16, h >> 8, h, 8, alpha ? 6 : 2, 0, 0, 0};
	chunk(f, "IHDR", ihdr, 13);
	chunk(f, "IDAT", z, (uint32_t) zp);
	chunk(f, "IEND", NULL, 0);
	int ok = (fclose(f) == 0);
	free(z);
	return ok;
}
