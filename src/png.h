#ifndef PNG_H
#define PNG_H
#include <stddef.h>
#include <stdint.h>

/* Decodifica un PNG (no entrelazado) a píxeles ARGB (0xAARRGGBB). Devuelve memoria de malloc o NULL. */
uint32_t* png_decode(const uint8_t* data, size_t len, int* w, int* h);
uint32_t* png_load_file(const char* path, int* w, int* h);
/* Guarda píxeles ARGB como PNG (sin compresión). Con alpha=0 se guarda RGB. Devuelve 1 si OK. */
int png_save_file(const char* path, const uint32_t* px, int w, int h, int stride_px, int alpha);

#endif
