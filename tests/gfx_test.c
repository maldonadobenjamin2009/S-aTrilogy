#include "../src/gfx.h"
#include "../src/png.h"
#include <stdio.h>
int main(void) {
	Canvas* c = cv_new(900, 520);
	cv_clear(c, RGB(12, 18, 44));
	gfx_rrect(c, rect_make(30, 30, 380, 120), 24, ARGB(255, 40, 140, 255));
	gfx_rrect_stroke(c, rect_make(30, 30, 380, 120), 24, 5, RGB(255, 255, 255));
	gfx_rrect(c, rect_make(440, 30, 200, 120), 18, ARGB(120, 255, 205, 60));
	gfx_circle(c, 740, 90, 50, RGB(255, 205, 60));
	gfx_ring(c, 740, 90, 36, 8, RGB(12, 18, 44));
	float poly[8] = {40, 200, 340, 200, 300, 270, 10, 270};
	gfx_poly(c, poly, 4, RGB(255, 205, 40));
	gfx_line(c, 380, 200, 600, 270, 6, RGB(255, 255, 255));
	gfx_text(c, "SONIC ADVANCE 2", 40, 300, 64, RGB(255, 255, 255), TX_BOLD);
	gfx_text(c, "Pausa · Guardar partida · Año 2002 ¿Cómo estás? áéíóú ñ Ñ", 40, 390, 26, RGB(215, 222, 240), 0);
	gfx_text(c, "CONTINUAR", 40, 440, 50, RGB(255, 205, 60), TX_BOLD | TX_ITALIC);
	gfx_text(c, "← → ↑ ↓ … 100%", 460, 440, 30, RGB(255, 255, 255), TX_BOLD);
	gfx_text(c, "Texto pequeño de 18 px para comprobar la legibilidad del SDF", 40, 490, 18, RGB(215, 222, 240), 0);
	png_save_file("/tmp/gfx_test.png", c->px, c->w, c->h, c->w, 0);
	printf("ok\n");
	return 0;
}
