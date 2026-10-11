#include "../src/png.h"
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char** argv) {
	int w, h;
	uint32_t* p = png_load_file(argv[1], &w, &h);
	if (!p) { printf("FAIL decode %s\n", argv[1]); return 1; }
	/* imprime tamaño y 3 píxeles */
	printf("%s %dx%d  p0=%08x pmid=%08x plast=%08x\n", argv[1], w, h, p[0], p[(size_t) (h / 2) * w + w / 2], p[(size_t) w * h - 1]);
	if (argc > 2) png_save_file(argv[2], p, w, h, w, 1);
	return 0;
}
