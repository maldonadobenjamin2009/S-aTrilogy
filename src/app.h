#ifndef APP_H
#define APP_H
#include <stddef.h>
#include <stdint.h>
#include "gfx.h"

/* =====================================================================================
 * Núcleo del launcher (portable): no usa SDL ni ninguna API de plataforma.
 * Cada plataforma (escritorio/SDL2, Android, PS4...) solo implementa:
 *   - enviar eventos (app_event)  - llamar a app_update/app_render cada fotograma
 *   - mostrar AppFrame            - reproducir el audio de app_take_audio
 * ===================================================================================== */

#define UI_W 1920
#define UI_H 1080

enum {
	EV_QUIT = 1,
	EV_KEY_DOWN,     /* code = tecla (ver keys.h), value = 1 si Alt está pulsado */
	EV_KEY_UP,       /* code = tecla */
	EV_PAD_DOWN,     /* code = botón (ids de SDL_GameControllerButton) */
	EV_PAD_UP,
	EV_PAD_AXIS,     /* code = eje (0/1 stick izq., 4/5 gatillos), value = -32768..32767 */
	EV_FOCUS_LOST,
	EV_MOUSE_MOVE,   /* code = x, value = y (coordenadas de la interfaz 1920x1080) */
	EV_MOUSE_DOWN,
	EV_PAD_COUNT,     /* value = número de mandos conectados */
	EV_TOUCH_DOWN,    /* code = ACT_* de config.h; botón virtual independiente del mapeo físico */
	EV_TOUCH_UP,
	EV_TOUCH_VOLUME  /* value = -10 o +10; flechas del volumen en la pausa */
};
typedef struct { int type, code, value, player; } AppEvent;

typedef struct {
	void* user;
	void (*open_folder)(void* user, const char* path);
	void (*set_fullscreen)(void* user, int on);
	void (*set_title)(void* user, const char* title);
	void (*set_cursor_visible)(void* user, int visible);
	const char* (*key_name)(void* user, int code);   /* opcional */
	int audio_rate;                                   /* frecuencia con la que la plataforma abrió el audio */
	int has_audio;
	const char* audio_info;
	const char* video_info;
} AppHost;

typedef struct {
	int in_game;               /* 1: hay que dibujar 'game' (240x160) */
	const uint32_t* game;      /* bytes R,G,B,X por píxel: jugador 1 */
	const uint32_t* game2;     /* jugador 2, o NULL */
	int multiplayer;           /* 1: mostrar game y game2 en dos mitades */
	int smooth, integer_scale;
	const Canvas* ui;          /* interfaz 1920x1080; en juego es una capa con transparencia. Puede ser NULL */
} AppFrame;

typedef struct App App;

App* app_create(const char* base_dir, const AppHost* host);
void app_destroy(App* a);
void app_event(App* a, const AppEvent* ev);
int app_update(App* a, double dt);                  /* devuelve 0 cuando hay que cerrar */
void app_render(App* a, AppFrame* out);
double app_frame_time(const App* a);                /* segundos que debe durar cada fotograma */
int app_take_audio(App* a, const int16_t** data);   /* fotogramas estéreo pendientes (ya con volumen) */
int app_audio_reset_requested(App* a);              /* 1 una vez tras pausar/reanudar/cargar */
void app_audio_feedback(App* a, int queued_frames);  /* ajuste suave para evitar retraso y cortes */

/* Estado auxiliar usado por la capa táctil SDL. */
int app_is_paused(const App* a);
int app_touch_menu_active(const App* a); /* hay una pantalla de menú sobre el juego */
int app_pause_volume_arrow_hit(const App* a, int ui_x, int ui_y); /* -10, +10 o 0 */

/* Para pruebas */
int app_debug_state(const App* a);

#endif
