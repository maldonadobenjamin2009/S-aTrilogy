#ifndef GFX_H
#define GFX_H
#include <stdint.h>

/* Lienzo de software: píxeles ARGB 0xAARRGGBB con alfa "recto" (no premultiplicado). */
typedef struct { int w, h; uint32_t* px; } Canvas;
typedef struct { int x, y, w, h; } Rect;

#define ARGB(a, r, g, b) ((((uint32_t) (a)) << 24) | (((uint32_t) (r)) << 16) | (((uint32_t) (g)) << 8) | ((uint32_t) (b)))
#define RGB(r, g, b) ARGB(255, r, g, b)
#define COL_ALPHA(c, a) (((c) & 0x00FFFFFFu) | (((uint32_t) (a)) << 24))

enum { TX_BOLD = 1, TX_ITALIC = 2, TX_CENTER = 4, TX_RIGHT = 8, TX_VMID = 16 };

static inline Rect rect_make(int x, int y, int w, int h) { Rect r = {x, y, w, h}; return r; }
static inline Rect rect_inflate(Rect r, int dx, int dy) { Rect o = {r.x - dx / 2, r.y - dy / 2, r.w + dx, r.h + dy}; return o; }
static inline Rect rect_move(Rect r, int dx, int dy) { Rect o = {r.x + dx, r.y + dy, r.w, r.h}; return o; }
static inline int rect_has(Rect r, int px, int py) { return px >= r.x && py >= r.y && px < r.x + r.w && py < r.y + r.h; }

Canvas* cv_new(int w, int h);
void cv_free(Canvas* c);
void cv_clear(Canvas* c, uint32_t color);
Canvas* cv_copy(const Canvas* c);
Canvas* cv_load_png(const char* path);
Canvas* cv_resize(const Canvas* src, int nw, int nh);     /* área al reducir, bilineal al ampliar */
Canvas* cv_cover(const Canvas* src, int w, int h);        /* rellena w x h recortando el centro */
Canvas* cv_fit(const Canvas* src, int maxw, int maxh);    /* cabe dentro conservando proporciones */
void cv_round_corners(Canvas* c, int radius);             /* deja transparentes las esquinas */
void cv_shade(Canvas* c, int alpha);                      /* superpone negro con ese alfa (filtro oscuro) */

void gfx_rect(Canvas* c, Rect r, uint32_t color);
void gfx_rrect(Canvas* c, Rect r, int radius, uint32_t color);
void gfx_rrect_stroke(Canvas* c, Rect r, int radius, int thickness, uint32_t color);
void gfx_circle(Canvas* c, float cx, float cy, float radius, uint32_t color);
void gfx_ring(Canvas* c, float cx, float cy, float radius, float thickness, uint32_t color);
void gfx_poly(Canvas* c, const float* xy, int n, uint32_t color);          /* relleno con suavizado */
void gfx_line(Canvas* c, float x0, float y0, float x1, float y1, float thickness, uint32_t color);
void gfx_blit(Canvas* dst, const Canvas* src, int x, int y, int alpha);     /* alpha 0..255 */
void gfx_blit_scaled(Canvas* dst, const Canvas* src, Rect to, int alpha);   /* bilineal */

int gfx_text(Canvas* c, const char* utf8, int x, int y, int size, uint32_t color, int flags);
int gfx_text_width(const char* utf8, int size, int flags);

#endif
