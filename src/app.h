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
	EV_MOUSE_DOWN
};
typedef struct { int type, code, value; } AppEvent;

typedef struct {
	void* user;
	void (*open_folder)(void* user, const char* path);
	void (*set_fullscreen)(void* user, int on);
	void (*set_title)(void* user, const char* title);
	void (*set_cursor_visible)(void* user, int visible);
	const char* (*key_name)(void* user, int code);   /* opcional */
	int audio_rate;                                   /* frecuencia con la que la plataforma abrió el audio */
	int has_audio;
	const char* audio_info;                           /* texto de diagnóstico (se muestra en Ajustes), opcional */
	const char* video_info;                           /* idem para el vídeo (renderizador) */
} AppHost;

/* Indicadores durante el juego (FPS, turbo, avisos): lienzos PEQUEÑOS, para no subir 8 MB por fotograma. */
#define HUD_FPS_W 520
#define HUD_FPS_H 140
#define HUD_FPS_X 24
#define HUD_FPS_Y 14
#define HUD_TOAST_W 1300
#define HUD_TOAST_H 70
#define HUD_TOAST_X ((UI_W - HUD_TOAST_W) / 2)
#define HUD_TOAST_Y 985
typedef struct { const Canvas* cv; int x, y; } HudItem;

typedef struct {
	int in_game;               /* 1: hay que dibujar 'game' (240x160) */
	const uint32_t* game;      /* bytes R,G,B,X por píxel */
	int smooth, integer_scale;
	const Canvas* ui;          /* interfaz 1920x1080; en juego es una capa con transparencia. Puede ser NULL */
	HudItem hud[2];            /* [0] FPS/turbo, [1] aviso; cv == NULL si no hay nada que mostrar */
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
/* Control dinámico de la tasa de audio: la plataforma indica cuántos fotogramas de audio hay en cola cada vez
 * que va a avanzar un fotograma. El ritmo del juego lo marca siempre el reloj; el audio se estira o encoge
 * (máx. ±1 %) para mantener la cola en ~40 ms, sin cortes ni retraso acumulado. */
void app_audio_feedback(App* a, int queued_frames);
/* La plataforma informa de cuánto tardó el último fotograma (emulación y mostrar en pantalla) para el indicador de FPS. */
void app_perf_report(App* a, double update_ms, double present_ms);

/* Para pruebas */
int app_debug_state(const App* a);

#endif
