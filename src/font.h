#ifndef FONT_H
#define FONT_H
typedef struct {
	int cp;            /* punto de código Unicode */
	short ax, ay;      /* posición en el atlas */
	short w, h;        /* tamaño en el atlas */
	float x0, y0;      /* esquina superior izquierda respecto al origen (línea base), en px del atlas */
	float adv;         /* avance horizontal en px del atlas */
} FontGlyph;
extern const int font_em, font_spread, font_atlas_w, font_atlas_h, font_glyph_count;
extern const float font_ascent, font_descent;
extern const FontGlyph font_glyphs[];
extern const unsigned char font_atlas[];
#endif
