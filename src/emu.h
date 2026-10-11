#ifndef EMU_API_H
#define EMU_API_H
#include <stdint.h>

/* Emulador GBA = núcleo de mGBA compilado dentro del propio ejecutable. */
#define EMU_W 240
#define EMU_H 160

typedef struct Emu Emu;

const char* emu_version(void);
/* save_path/bios_path pueden ser NULL. audio_rate = frecuencia de salida (p. ej. 48000). */
Emu* emu_open(const char* rom_path, const char* save_path, const char* bios_path, unsigned audio_rate);
void emu_close(Emu* e);
void emu_reset(Emu* e);
void emu_set_keys(Emu* e, uint32_t keys);            /* bits: A,B,SELECT,START,RIGHT,LEFT,UP,DOWN,R,L */
void emu_run_frame(Emu* e);
const uint32_t* emu_video(Emu* e);                    /* 240x160, bytes R,G,B,X (uint32 0xXXBBGGRR) */
int emu_read_audio(Emu* e, int16_t* dst, int max_frames);   /* muestras estéreo ya remuestreadas */
void emu_clear_audio(Emu* e);
int emu_save_state(Emu* e, const char* path);
int emu_load_state(Emu* e, const char* path);

#endif
