#ifndef EMU_API_H
#define EMU_API_H
#include <stdint.h>

/* Emulador GBA = núcleo de mGBA compilado dentro del propio ejecutable. */
#define EMU_W 240
#define EMU_H 160

typedef struct Emu Emu;

const char* emu_version(void);
void emu_enable_debug_logging(void);
/* save_path/bios_path pueden ser NULL. audio_rate = frecuencia de salida (p. ej. 48000). */
Emu* emu_open(const char* rom_path, const char* save_path, const char* bios_path, unsigned audio_rate);
/* Abre dos núcleos de la misma ROM conectados al cable link de mGBA. J1 es la instancia devuelta por emu_close; J2 se devuelve en secondary_out. */
Emu* emu_open_linked_pair(const char* rom_path, const char* save_path, const char* save_path2, const char* bios_path, unsigned audio_rate, Emu** secondary_out);
Emu* emu_open_linked_trio(const char* rom_path, const char* save_path, const char* save_path2, const char* save_path3, const char* bios_path, unsigned audio_rate, Emu** secondary_out, Emu** tertiary_out);
void emu_close(Emu* e);
void emu_reset(Emu* e);
void emu_set_paused(Emu* e, int paused); /* pausa/reanuda ambos núcleos enlazados */
void emu_set_keys(Emu* e, uint32_t keys);            /* bits: A,B,SELECT,START,RIGHT,LEFT,UP,DOWN,R,L */
void emu_run_frame(Emu* e);
const uint32_t* emu_video(Emu* e);                    /* compatibilidad; no usar para lectura concurrente */
void emu_copy_video(Emu* e, uint32_t* dst);            /* copia segura del último fotograma, 240x160 */
int emu_read_audio(Emu* e, int16_t* dst, int max_frames);   /* muestras estéreo ya remuestreadas */
void emu_clear_audio(Emu* e);
void emu_set_audio_rate(Emu* e, double rate);
int emu_save_state(Emu* e, const char* path);
int emu_load_state(Emu* e, const char* path);

#endif
