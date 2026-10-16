/* main_sdl.c - plataforma SDL2 para escritorio y Android.
 * Gestiona ventana/vídeo/audio/mandos, traduce eventos a AppEvent y presenta AppFrame.
 * En Android añade controles táctiles sobre el juego. */
#define SDL_MAIN_HANDLED
#include <SDL.h>
#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif
#if defined(__ANDROID__)
#include <SDL_system.h>
#endif
#if defined(__ORBIS__) || defined(PS4)
#include <orbis/VideoOut.h>
#include <sys/stat.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "app.h"
#include "emu.h"
#include "keys.h"
#include "config.h"
#include "png.h"
#include "debug_log.h"

#if defined(_WIN32)
#include <windows.h>
#include <shellapi.h>
#endif

#if defined(__ANDROID__)
typedef struct {
	SDL_FingerID id;
	int active;
	int kind;             /* 1 = botón, 2 = palanca, 3 = menú, 4 = flecha de volumen */
	int code;
	int code2;            /* segunda dirección si la palanca está en diagonal */
} TouchPoint;
#endif

typedef struct {
	SDL_Window* win;
	SDL_Renderer* ren;
	SDL_Texture *tex_ui, *tex_game_sharp, *tex_game_smooth, *tex_game2_sharp, *tex_game2_smooth;
	SDL_AudioDeviceID audio;
	SDL_GameController* pads[16];
	int fullscreen;
#if defined(__ORBIS__) || defined(PS4)
	/* SDL-PS4 presenta el contenido mediante la superficie de ventana. */
	SDL_Surface* window_surface;
#endif
#if defined(__ANDROID__)
	int last_in_game;
	TouchPoint touches[16];
	SDL_Texture* tex_touch;
	int touch_w, touch_h, touch_target_failed, touch_menu_overlay;
	SDL_FingerID stick_finger;
	float stick_visual_x, stick_visual_y; /* desplazamiento de la palanca, -1..1 */
#endif
} Plat;


static Plat P;

#if defined(_WIN32)
static void open_debug_console(void) {
    if (!GetConsoleWindow()) {
        if (!AttachConsole(ATTACH_PARENT_PROCESS)) AllocConsole();
    }
    (void) freopen("CONOUT$", "w", stdout);
    (void) freopen("CONOUT$", "w", stderr);
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    SetConsoleTitleA("Sonic Advance Trilogy - Depuración");
    printf("Sonic Advance Trilogy: consola de depuración activa.\n");
}
#endif

/* ------------------------------------------------------------- servicios para la app */
static void h_open_folder(void* u, const char* path) {
	(void) u;
#if defined(_WIN32)
	ShellExecuteA(NULL, "open", path, NULL, NULL, SW_SHOWNORMAL);
#elif defined(__ANDROID__)
	/* Android no permite abrir una ruta arbitraria como carpeta mediante SDL. */
	(void) path;
#elif defined(__ORBIS__) || defined(PS4) || defined(__EMSCRIPTEN__)
	/* En consolas y en navegador no hay un explorador de carpetas que lanzar. */
	(void) path;
#elif defined(__APPLE__)
	char cmd[1200]; snprintf(cmd, sizeof(cmd), "open \"%s\" &", path); if (system(cmd)) {}
#else
	char cmd[1200]; snprintf(cmd, sizeof(cmd), "xdg-open \"%s\" >/dev/null 2>&1 &", path); if (system(cmd)) {}
#endif
}
static void h_fullscreen(void* u, int on) { (void) u; P.fullscreen = on; SDL_SetWindowFullscreen(P.win, on ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0); }
static void h_title(void* u, const char* t) { (void) u; SDL_SetWindowTitle(P.win, t); }
static void h_cursor(void* u, int v) { (void) u; SDL_ShowCursor(v ? SDL_ENABLE : SDL_DISABLE); }
static const char* h_key_name(void* u, int code) { (void) u; return SDL_GetKeyName((SDL_Keycode) code); }

/* ------------------------------------------------------------------------- audio
 * Igual que el frontend SDL oficial de mGBA:
 *  - en Windows se prefiere DirectSound (el WASAPI de SDL daba latencia y cortes; ver CHANGES de mGBA),
 *  - se permite que el dispositivo cambie la frecuencia (usamos la que nos dé como frecuencia de salida),
 *  - el ritmo del juego lo marca SIEMPRE el reloj (59,7275 fps); la tasa del audio se corrige ±1 % según la
 *    cola (control dinámico, app_audio_feedback) para que no haya retraso acumulado ni cortes. Si el dispositivo
 *    de audio consumiera a otra velocidad, el juego NO se ralentiza. */
#define AUDIO_DEVICE_SAMPLES 1024     /* buffer del dispositivo: mismo valor por defecto que mGBA (audioBuffers = 1024) */
#define AUDIO_MAX_MS 250              /* si se pasa de esto (p. ej. tras arrastrar la ventana) se vacía */
#define AUDIO_PREROLL_MS 40           /* colchón de silencio al empezar/reanudar (igual que la cola objetivo de app_audio_feedback) */

static SDL_AudioDeviceID open_audio(SDL_AudioSpec* have) {
	SDL_AudioSpec want;
	SDL_zero(want);
	want.freq = 48000; want.format = AUDIO_S16SYS; want.channels = 2; want.samples = AUDIO_DEVICE_SAMPLES;
	const char* drivers[3];
	int n = 0;
	const char* preferred_driver = SDL_getenv("SDL_AUDIODRIVER");
#if defined(_WIN32)
	if (!preferred_driver) { drivers[n++] = "directsound"; drivers[n++] = "wasapi"; }
#endif
	drivers[n++] = NULL;
	for (int i = 0; i < n; i++) {
		if (drivers[i]) SDL_setenv("SDL_AUDIODRIVER", drivers[i], 1);
		else if (!preferred_driver) SDL_setenv("SDL_AUDIODRIVER", "", 1);
		if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) continue;
		SDL_AudioDeviceID d = SDL_OpenAudioDevice(NULL, 0, &want, have, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
		if (d && (have->channels != 2 || have->format != AUDIO_S16SYS)) {   /* que SDL convierta: necesitamos s16 estéreo */
			SDL_CloseAudioDevice(d);
			d = SDL_OpenAudioDevice(NULL, 0, &want, have, 0);
		}
		if (d) return d;
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
	}
	return 0;
}

/* -------------------------------------------------------------------------- vídeo */
static SDL_Texture* make_tex(int fmt, int w, int h, int linear, SDL_BlendMode bm) {
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, linear ? "1" : "0");
	SDL_Texture* t = SDL_CreateTexture(P.ren, fmt, SDL_TEXTUREACCESS_STREAMING, w, h);
	if (t) SDL_SetTextureBlendMode(t, bm);
	return t;
}

static void ui_dest(int ow, int oh, SDL_Rect* d) {
	float s = (float) ow / UI_W < (float) oh / UI_H ? (float) ow / UI_W : (float) oh / UI_H;
	d->w = (int) (UI_W * s); d->h = (int) (UI_H * s);
	d->x = (ow - d->w) / 2; d->y = (oh - d->h) / 2;
}

#if defined(__ANDROID__)
/* Controles táctiles nativos de la compilación Android. Todos emiten los mismos
 * eventos que un GameController; la cruceta se presenta como palanca analógica. */
enum { TOUCH_ACTION = 1, TOUCH_STICK = 2, TOUCH_MENU = 3, TOUCH_VOLUME = 4 };

typedef struct { int kind, code, code2; } TouchHit;
static void send_app_event_j1(App* app, int type, int code, int value);
static void send_player(App* app, int type, int code, int value, int player);
static void mouse_to_ui(int wx, int wy, int* ux, int* uy);
static void touch_release_all(App* app);

static void touch_geometry(int w, int h, int *mini, int *stick_r, int *button_r, int *shoulder_r) {
	*mini = w < h ? w : h;
	*stick_r = (int) (*mini * 0.135f);
	*button_r = (int) (*mini * 0.070f);
	*shoulder_r = (int) (*mini * 0.055f);
}

/* Calcula una entrada digital GBA a partir del vector de la palanca. En la zona
 * diagonal se envían dos direcciones para permitir saltos/movimientos diagonales. */
static TouchHit touch_stick_value(App* app, int x, int y, int w, int h) {
	int mini, sr, br, shr;
	touch_geometry(w, h, &mini, &sr, &br, &shr);
	(void) br; (void) shr;
	int cx = (int)(w * (app_touch_menu_active(app) ? 0.62f : 0.16f));
	int cy = (int)(h * 0.735f);
	int dx = x - cx, dy = y - cy;
	TouchHit z = {TOUCH_STICK, -1, -1};
	int dead = (int)(sr * 0.18f);
	if (dx * dx + dy * dy <= dead * dead) return z;
	int ax = abs(dx), ay = abs(dy);
	int xcode = dx < 0 ? ACT_LEFT : ACT_RIGHT;
	int ycode = dy < 0 ? ACT_UP : ACT_DOWN;
	if (ax > 0 && ay > 0 && ax * 100 >= ay * 42 && ay * 100 >= ax * 42) {
		z.code = ycode;
		z.code2 = xcode;
	} else if (ax >= ay) z.code = xcode;
	else z.code = ycode;
	return z;
}

static TouchHit touch_hit(App* app, int x, int y, int w, int h) {
	int mini, sr, br, shr;
	touch_geometry(w, h, &mini, &sr, &br, &shr);
	TouchHit none = {0, -1, -1};
	/* Pausa arriba, centrada. */
	int cx = w / 2, cy = (int) (h * 0.095f);
	int dx = x - cx, dy = y - cy;
	int pr = (int) (mini * 0.060f);
	if (dx * dx + dy * dy <= pr * pr) { TouchHit z = {TOUCH_ACTION, ACT_PAUSE, -1}; return z; }
	/* L y R en las esquinas superiores. */
	cx = (int) (w * 0.085f); cy = (int) (h * 0.105f);
	dx = x - cx; dy = y - cy;
	if (dx * dx + dy * dy <= shr * shr) { TouchHit z = {TOUCH_ACTION, ACT_L, -1}; return z; }
	cx = (int) (w * 0.915f); cy = (int) (h * 0.105f);
	dx = x - cx; dy = y - cy;
	if (dx * dx + dy * dy <= shr * shr) { TouchHit z = {TOUCH_ACTION, ACT_R, -1}; return z; }
	/* Área de la palanca: se conserva el mismo centro que el dibujo. */
	cx = (int) (w * (app_touch_menu_active(app) ? 0.62f : 0.16f)); cy = (int) (h * 0.735f);
	dx = x - cx; dy = y - cy;
	int stick_hit_r = (int)(sr * 1.08f);
	if (dx * dx + dy * dy <= stick_hit_r * stick_hit_r) return touch_stick_value(app, x, y, w, h);
	/* Botones A y B. */
	cx = (int) (w * 0.865f); cy = (int) (h * 0.745f);
	dx = x - cx; dy = y - cy;
	if (dx * dx + dy * dy <= br * br) { TouchHit z = {TOUCH_ACTION, ACT_A, -1}; return z; }
	cx = (int) (w * 0.755f); cy = (int) (h * 0.795f);
	dx = x - cx; dy = y - cy;
	if (dx * dx + dy * dy <= br * br) { TouchHit z = {TOUCH_ACTION, ACT_B, -1}; return z; }
	/* Select/Start centrados abajo, en dos superficies tipo pastilla. */
	int small_r = (int)(mini * 0.050f), btn_y = (int)(h * 0.925f);
	int select_x = (int)(w * 0.455f), start_x = (int)(w * 0.545f);
	if (abs(x - select_x) <= small_r * 2 && abs(y - btn_y) <= small_r) { TouchHit z = {TOUCH_ACTION, ACT_SELECT, -1}; return z; }
	if (abs(x - start_x) <= small_r * 2 && abs(y - btn_y) <= small_r) { TouchHit z = {TOUCH_ACTION, ACT_START, -1}; return z; }
	return none;
}

static void touch_send_hit(App* app, const TouchHit* hit, int down) {
	if (!hit || (hit->kind != TOUCH_ACTION && hit->kind != TOUCH_STICK)) return;
	if (hit->code >= 0) send_app_event_j1(app, down ? EV_TOUCH_DOWN : EV_TOUCH_UP, hit->code, 0);
	if (hit->code2 >= 0 && hit->code2 != hit->code)
		send_app_event_j1(app, down ? EV_TOUCH_DOWN : EV_TOUCH_UP, hit->code2, 0);
}

static void touch_update_stick_visual(App* app, int x, int y, int w, int h) {
	int mini, sr, br, shr;
	touch_geometry(w, h, &mini, &sr, &br, &shr);
	(void) mini; (void) br; (void) shr;
	int cx = (int)(w * (app_touch_menu_active(app) ? 0.62f : 0.16f));
	int cy = (int)(h * 0.735f);
	float maxr = sr * 0.62f;
	float dx = (float)(x - cx), dy = (float)(y - cy);
	float len = sqrtf(dx * dx + dy * dy);
	if (len > maxr && len > 0.0f) { dx *= maxr / len; dy *= maxr / len; }
	P.stick_visual_x = maxr > 0 ? dx / maxr : 0.0f;
	P.stick_visual_y = maxr > 0 ? dy / maxr : 0.0f;
}

static TouchPoint* touch_find(SDL_FingerID id, int create) {
	TouchPoint* free_slot = NULL;
	for (int i = 0; i < (int)(sizeof(P.touches) / sizeof(P.touches[0])); i++) {
		if (P.touches[i].active && P.touches[i].id == id) return &P.touches[i];
		if (!P.touches[i].active && !free_slot) free_slot = &P.touches[i];
	}
	if (create && free_slot) {
		memset(free_slot, 0, sizeof(*free_slot));
		free_slot->active = 1;
		free_slot->id = id;
		free_slot->code = free_slot->code2 = -1;
		return free_slot;
	}
	return NULL;
}

static void touch_event_down(App* app, SDL_TouchFingerEvent* ev) {
	int ww, wh, ow, oh;
	SDL_GetWindowSize(P.win, &ww, &wh);
	SDL_GetRendererOutputSize(P.ren, &ow, &oh);
	int wx = (int)(ev->x * (ww > 0 ? ww : ow));
	int wy = (int)(ev->y * (wh > 0 ? wh : oh));
	int ux = 0, uy = 0;
	mouse_to_ui(wx, wy, &ux, &uy);
	TouchPoint* tp = touch_find(ev->fingerId, 1);
	if (!tp) return;
	/* Las flechas del volumen deben seguir activas mientras el menú está en pausa. */
	if (P.last_in_game) {
		int volume_delta = app_pause_volume_arrow_hit(app, ux, uy);
		if (volume_delta) {
			tp->kind = TOUCH_VOLUME; tp->code = volume_delta; tp->code2 = -1;
			send_app_event_j1(app, EV_TOUCH_VOLUME, 0, volume_delta);
			return;
		}
	}
	if (P.last_in_game && !app_touch_menu_active(app)) {
		TouchHit hit = touch_hit(app, (int)(ev->x * ow), (int)(ev->y * oh), ow, oh);
		if (hit.kind == TOUCH_ACTION && hit.code == ACT_PAUSE) {
			touch_release_all(app);
			tp = touch_find(ev->fingerId, 1);
			if (!tp) return;
		}
		tp->kind = hit.kind; tp->code = hit.code; tp->code2 = hit.code2;
		if (hit.kind == TOUCH_STICK) {
			P.stick_finger = ev->fingerId;
			touch_update_stick_visual(app, (int)(ev->x * ow), (int)(ev->y * oh), ow, oh);
		}
		touch_send_hit(app, &hit, 1);
	} else {
		tp->kind = TOUCH_MENU;
		mouse_to_ui(wx, wy, &ux, &uy);
		send_app_event_j1(app, EV_MOUSE_MOVE, ux, uy);
		send_app_event_j1(app, EV_MOUSE_DOWN, ux, uy);
	}
}

static void touch_event_motion(App* app, SDL_TouchFingerEvent* ev) {
	TouchPoint* tp = touch_find(ev->fingerId, 0);
	if (!tp) return;
	if (tp->kind == TOUCH_STICK) {
		int ow, oh;
		SDL_GetRendererOutputSize(P.ren, &ow, &oh);
		int x = (int)(ev->x * ow), y = (int)(ev->y * oh);
		touch_update_stick_visual(app, x, y, ow, oh);
		TouchHit next = touch_stick_value(app, x, y, ow, oh);
		if (next.code != tp->code || next.code2 != tp->code2) {
			TouchHit old = {TOUCH_STICK, tp->code, tp->code2};
			touch_send_hit(app, &old, 0);
			tp->code = next.code; tp->code2 = next.code2;
			touch_send_hit(app, &next, 1);
		}
	} else if (tp->kind == TOUCH_MENU) {
		int ww, wh, x, y;
		SDL_GetWindowSize(P.win, &ww, &wh);
		mouse_to_ui((int)(ev->x * ww), (int)(ev->y * wh), &x, &y);
		send_app_event_j1(app, EV_MOUSE_MOVE, x, y);
	}
}

static void touch_event_up(App* app, SDL_TouchFingerEvent* ev) {
	TouchPoint* tp = touch_find(ev->fingerId, 0);
	if (!tp) return;
	TouchHit hit = {tp->kind, tp->code, tp->code2};
	touch_send_hit(app, &hit, 0);
	if (tp->kind == TOUCH_STICK && P.stick_finger == ev->fingerId) {
		P.stick_visual_x = P.stick_visual_y = 0.0f;
	}
	memset(tp, 0, sizeof(*tp));
}

static void touch_release_all(App* app) {
	for (int i = 0; i < (int)(sizeof(P.touches) / sizeof(P.touches[0])); i++) {
		TouchPoint* tp = &P.touches[i];
		if (!tp->active) continue;
		TouchHit hit = {tp->kind, tp->code, tp->code2};
		touch_send_hit(app, &hit, 0);
		memset(tp, 0, sizeof(*tp));
	}
	P.stick_visual_x = P.stick_visual_y = 0.0f;
}

static void draw_circle(int cx, int cy, int r, Uint8 red, Uint8 green, Uint8 blue, Uint8 alpha) {
	SDL_SetRenderDrawColor(P.ren, red, green, blue, alpha);
	for (int y = -r; y <= r; y++) {
		int x = (int)sqrt((double)r * r - (double)y * y);
		SDL_RenderDrawLine(P.ren, cx - x, cy + y, cx + x, cy + y);
	}
}

static void draw_circle_outline(int cx, int cy, int r, Uint8 red, Uint8 green, Uint8 blue, Uint8 alpha) {
	SDL_SetRenderDrawColor(P.ren, red, green, blue, alpha);
	int x = r, y = 0, err = 1 - x;
	while (x >= y) {
		SDL_RenderDrawPoint(P.ren, cx + x, cy + y); SDL_RenderDrawPoint(P.ren, cx + y, cy + x);
		SDL_RenderDrawPoint(P.ren, cx - y, cy + x); SDL_RenderDrawPoint(P.ren, cx - x, cy + y);
		SDL_RenderDrawPoint(P.ren, cx - x, cy - y); SDL_RenderDrawPoint(P.ren, cx - y, cy - x);
		SDL_RenderDrawPoint(P.ren, cx + y, cy - x); SDL_RenderDrawPoint(P.ren, cx + x, cy - y);
		y++;
		if (err < 0) err += 2 * y + 1; else { x--; err += 2 * (y - x + 1); }
	}
}

static const Uint8* touch_glyph(char c) {
	static const Uint8 A[7]={14,17,17,31,17,17,17}, B[7]={30,17,17,30,17,17,30};
	static const Uint8 C[7]={14,17,16,16,16,17,14}, E[7]={31,16,16,30,16,16,31};
	static const Uint8 L[7]={16,16,16,16,16,16,31}, P[7]={30,17,17,30,16,16,16};
	static const Uint8 R[7]={30,17,17,30,20,18,17}, S[7]={15,16,16,14,1,1,30};
	static const Uint8 T[7]={31,4,4,4,4,4,4}, U[7]={17,17,17,17,17,17,14};
	switch (c) { case 'A': return A; case 'B': return B; case 'C': return C; case 'E': return E; case 'L': return L;
	case 'P': return P; case 'R': return R; case 'S': return S; case 'T': return T; case 'U': return U; default: return NULL; }
}

static void draw_touch_label(const char* text, int cx, int cy, int scale) {
	int len = (int)strlen(text), total = len ? len * 6 * scale - scale : 0;
	int x0 = cx - total / 2, y0 = cy - (7 * scale) / 2;
	SDL_SetRenderDrawColor(P.ren, 255, 255, 255, 235);
	for (int i = 0; i < len; i++) {
		const Uint8* glyph = touch_glyph(text[i]);
		if (!glyph) continue;
		for (int y = 0; y < 7; y++) for (int x = 0; x < 5; x++) {
			if (glyph[y] & (1 << (4 - x))) {
				SDL_Rect px = {x0 + i * 6 * scale + x * scale, y0 + y * scale, scale, scale};
				SDL_RenderFillRect(P.ren, &px);
			}
		}
	}
}

static void draw_button(int cx, int cy, int r, const char* label, int label_scale) {
	draw_circle(cx, cy, r, 18, 28, 48, 135);
	draw_circle_outline(cx, cy, r, 235, 242, 255, 220);
	if (label) draw_touch_label(label, cx, cy, label_scale);
}

static void draw_touch_controls(int w, int h, int paused) {
	int mini, sr, br, shr;
	touch_geometry(w, h, &mini, &sr, &br, &shr);
	SDL_SetRenderDrawBlendMode(P.ren, SDL_BLENDMODE_BLEND);
	/* Base de palanca: anillos concéntricos y cavidad, sin las cuatro flechas de cruceta. */
	int dx = (int)(w * (paused ? .62f : .16f)), dy = (int)(h * .735f);
	draw_circle(dx, dy, sr, 8, 13, 27, 155);
	draw_circle_outline(dx, dy, sr, 235, 242, 255, 205);
	draw_circle(dx, dy, (int)(sr*.78f), 34, 45, 66, 145);
	draw_circle_outline(dx, dy, (int)(sr*.78f), 108, 126, 158, 180);
	/* A/B a la derecha. */
	draw_button((int)(w*.865f), (int)(h*.745f), br, "A", 3);
	draw_button((int)(w*.755f), (int)(h*.795f), br, "B", 3);
	/* Botones superiores L/R y pausa. */
	draw_button((int)(w*.085f), (int)(h*.105f), shr, "L", 2);
	draw_button((int)(w*.915f), (int)(h*.105f), shr, "R", 2);
	draw_button(w/2, (int)(h*.095f), (int)(mini*.060f), "PAUSA", 1);
	/* Select y Start centrados abajo. */
	int rr = (int)(mini*.050f), y = (int)(h*.925f), sx=(int)(w*.455f), tx=(int)(w*.545f);
	SDL_Rect sel={sx-rr*2,y-rr,rr*4,rr*2}, sta={tx-rr*2,y-rr,rr*4,rr*2};
	SDL_SetRenderDrawColor(P.ren,18,28,48,145); SDL_RenderFillRect(P.ren,&sel); SDL_RenderFillRect(P.ren,&sta);
	SDL_SetRenderDrawColor(P.ren,235,242,255,220); SDL_RenderDrawRect(P.ren,&sel); SDL_RenderDrawRect(P.ren,&sta);
	draw_touch_label("SELECT",sx,y,1); draw_touch_label("START",tx,y,1);
	SDL_SetRenderDrawBlendMode(P.ren, SDL_BLENDMODE_NONE);
}

/* El mando visual se dibuja dinámicamente sobre la base estática y sigue el dedo. */
static void draw_touch_stick_knob(int w, int h, int paused) {
	int mini, sr, br, shr;
	touch_geometry(w, h, &mini, &sr, &br, &shr);
	(void) mini; (void) br; (void) shr;
	int cx = (int)(w * (paused ? .62f : .16f)), cy = (int)(h * .735f);
	int sx = cx + (int)(P.stick_visual_x * sr * .62f);
	int sy = cy + (int)(P.stick_visual_y * sr * .62f);
	int knob = (int)(sr * .31f);
	SDL_SetRenderDrawBlendMode(P.ren, SDL_BLENDMODE_BLEND);
	/* Vástago corto desde el pivote hasta el pomo, con una sombra para dar profundidad. */
	SDL_SetRenderDrawColor(P.ren, 3, 6, 12, 170);
	SDL_RenderDrawLine(P.ren, cx+3, cy+4, sx+3, sy+4);
	SDL_SetRenderDrawColor(P.ren, 105, 121, 151, 230);
	SDL_RenderDrawLine(P.ren, cx, cy, sx, sy);
	draw_circle(sx, sy, knob, 26, 36, 57, 245);
	draw_circle_outline(sx, sy, knob, 225, 235, 255, 245);
	draw_circle(sx - knob/3, sy - knob/3, knob/4, 132, 151, 185, 210);
	SDL_SetRenderDrawBlendMode(P.ren, SDL_BLENDMODE_NONE);
}

/* El overlay de botones no cambia por toque: cachearlo evita cientos de primitivas por fotograma. */
static void ensure_touch_texture(int w, int h, int paused) {
	if (P.touch_target_failed) return;
	if (P.tex_touch && P.touch_w == w && P.touch_h == h && P.touch_menu_overlay == paused) return;
	if (P.tex_touch) { SDL_DestroyTexture(P.tex_touch); P.tex_touch = NULL; }
	P.tex_touch = SDL_CreateTexture(P.ren, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, w, h);
	if (!P.tex_touch) { P.touch_target_failed = 1; return; }
	SDL_SetTextureBlendMode(P.tex_touch, SDL_BLENDMODE_BLEND);
	if (SDL_SetRenderTarget(P.ren, P.tex_touch) != 0) {
		SDL_DestroyTexture(P.tex_touch); P.tex_touch = NULL; P.touch_target_failed = 1; return;
	}
	SDL_SetRenderDrawBlendMode(P.ren, SDL_BLENDMODE_NONE);
	SDL_SetRenderDrawColor(P.ren, 0, 0, 0, 0);
	SDL_RenderClear(P.ren);
	draw_touch_controls(w, h, paused);
	SDL_SetRenderTarget(P.ren, NULL);
	P.touch_w = w; P.touch_h = h; P.touch_menu_overlay = paused;
}
#endif

static void present(App* app, const AppFrame* f) {
	(void) app;
	int ow, oh;
#if defined(__ORBIS__) || defined(PS4)
	/* SDL_CreateSoftwareRenderer trabaja directamente sobre esta superficie y
	 * no todas las builds devuelven tamaño válido desde GetRendererOutputSize. */
	ow = P.window_surface ? P.window_surface->w : 1280;
	oh = P.window_surface ? P.window_surface->h : 720;
#else
	SDL_GetRendererOutputSize(P.ren, &ow, &oh);
#endif
	SDL_SetRenderDrawColor(P.ren, 0, 0, 0, 255);
	SDL_RenderClear(P.ren);
	if (f->in_game && f->game) {
		int split = f->multiplayer && f->game2;
		int half_w = split ? ow / 2 : ow;
		float s = (float) half_w / EMU_W < (float) oh / EMU_H ? (float) half_w / EMU_W : (float) oh / EMU_H;
		if (f->integer_scale && s >= 1.0f) s = (float) (int) s;
		int dw = (int) (EMU_W * s), dh = (int) (EMU_H * s);
		SDL_Rect d = {(half_w - dw) / 2, (oh - dh) / 2, dw, dh};
		SDL_Texture* t = f->smooth ? P.tex_game_smooth : P.tex_game_sharp;
		SDL_UpdateTexture(t, NULL, f->game, EMU_W * 4);
		SDL_RenderCopy(P.ren, t, NULL, &d);
		if (split) {
			SDL_Rect d2 = {half_w + (half_w - dw) / 2, (oh - dh) / 2, dw, dh};
			SDL_Texture* t2 = f->smooth ? P.tex_game2_smooth : P.tex_game2_sharp;
			SDL_UpdateTexture(t2, NULL, f->game2, EMU_W * 4);
			SDL_RenderCopy(P.ren, t2, NULL, &d2);
			SDL_SetRenderDrawColor(P.ren, 70, 80, 112, 255);
			SDL_RenderDrawLine(P.ren, half_w, 0, half_w, oh);
		}
	}
	if (f->ui) {
		SDL_UpdateTexture(P.tex_ui, NULL, f->ui->px, UI_W * 4);
		SDL_Rect d;
		ui_dest(ow, oh, &d);
		SDL_RenderCopy(P.ren, P.tex_ui, NULL, &d);
	}
#if defined(__ANDROID__)
	P.last_in_game = f->in_game;
	if (f->in_game) {
		int paused = app_touch_menu_active(app);
		ensure_touch_texture(ow, oh, paused);
		if (P.tex_touch) SDL_RenderCopy(P.ren, P.tex_touch, NULL, NULL);
		else if (P.touch_target_failed) draw_touch_controls(ow, oh, paused);
		draw_touch_stick_knob(ow, oh, paused);
	}
#endif
#if defined(__ORBIS__) || defined(PS4)
	/* El port SDL2 de OpenOrbis requiere una actualización explícita de la
	 * superficie de ventana para que el fotograma llegue a la salida de vídeo. */
	static unsigned surface_present_errors = 0;
	if (SDL_UpdateWindowSurface(P.win) != 0) {
		if (surface_present_errors < 5) {
			debug_log_error("PS4: SDL_UpdateWindowSurface falló (%u): %s",
			                surface_present_errors + 1, SDL_GetError());
		}
		surface_present_errors++;
	}
#else
	SDL_RenderPresent(P.ren);
#endif
}

/* ------------------------------------------------------------------------- entrada */
static void open_pad(int index) {
	if (!SDL_IsGameController(index)) return;
	for (int i = 0; i < 16; i++) if (!P.pads[i]) { P.pads[i] = SDL_GameControllerOpen(index); return; }
}
static void close_pad(SDL_JoystickID id) {
	for (int i = 0; i < 16; i++)
		if (P.pads[i] && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(P.pads[i])) == id) { SDL_GameControllerClose(P.pads[i]); P.pads[i] = NULL; }
}

static int map_key(SDL_Keycode k) {
	if (k == SDLK_KP_ENTER) return K_RETURN;
#if defined(__ANDROID__)
	if (k == SDLK_AC_BACK) return K_ESCAPE;
#endif
	return (int) k;
}

static void send_player(App* app, int type, int code, int value, int player) {
	AppEvent e = {type, code, value, player};
	app_event(app, &e);
}
static void send_app_event_j1(App* app, int type, int code, int value) { send_player(app, type, code, value, 0); }
static int opened_pad_count(void) {
	int n = 0;
	for (int i = 0; i < 16; i++) if (P.pads[i]) n++;
	return n;
}
static int pad_slot_for_instance(SDL_JoystickID id) {
	/* El índice físico puede dejar huecos al desconectar un mando. Convertimos
	 * cada dispositivo a su orden entre los mandos realmente abiertos. */
	int player = 0;
	for (int i = 0; i < 16; i++) {
		if (!P.pads[i]) continue;
		if (SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(P.pads[i])) == id) return player;
		player++;
	}
	return -1;
}
static void send_pad_event(App* app, int type, int code, int value, SDL_JoystickID id) {
	int player = pad_slot_for_instance(id);
	if (player >= 0 && player < 2) send_player(app, type, code, value, player);
}

static void mouse_to_ui(int wx, int wy, int* ux, int* uy) {
	int ow, oh, ww, wh;
	SDL_GetRendererOutputSize(P.ren, &ow, &oh);
	SDL_GetWindowSize(P.win, &ww, &wh);
	float fx = ww > 0 ? (float) ow / ww : 1.0f, fy = wh > 0 ? (float) oh / wh : 1.0f;
	SDL_Rect d;
	ui_dest(ow, oh, &d);
	*ux = (int) ((wx * fx - d.x) * UI_W / (float) d.w);
	*uy = (int) ((wy * fy - d.y) * UI_H / (float) d.h);
}

static void pump_events(App* app) {
	SDL_Event e;
	while (SDL_PollEvent(&e)) {
		switch (e.type) {
		case SDL_QUIT: send_app_event_j1(app, EV_QUIT, 0, 0); break;
		case SDL_KEYDOWN: if (!e.key.repeat) send_app_event_j1(app, EV_KEY_DOWN, map_key(e.key.keysym.sym), (e.key.keysym.mod & KMOD_ALT) ? 1 : 0); break;
		case SDL_KEYUP: send_app_event_j1(app, EV_KEY_UP, map_key(e.key.keysym.sym), 0); break;
		case SDL_CONTROLLERDEVICEADDED: open_pad(e.cdevice.which); debug_log_info("Mando conectado; mandos reconocidos: %d", opened_pad_count()); send_app_event_j1(app, EV_PAD_COUNT, 0, opened_pad_count()); break;
		case SDL_CONTROLLERDEVICEREMOVED: close_pad(e.cdevice.which); debug_log_info("Mando desconectado; mandos reconocidos: %d", opened_pad_count()); send_app_event_j1(app, EV_PAD_COUNT, 0, opened_pad_count()); break;
		case SDL_CONTROLLERBUTTONDOWN: send_pad_event(app, EV_PAD_DOWN, e.cbutton.button, 0, e.cbutton.which); break;
		case SDL_CONTROLLERBUTTONUP: send_pad_event(app, EV_PAD_UP, e.cbutton.button, 0, e.cbutton.which); break;
		case SDL_CONTROLLERAXISMOTION: send_pad_event(app, EV_PAD_AXIS, e.caxis.axis, e.caxis.value, e.caxis.which); break;
		case SDL_WINDOWEVENT: if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
#if defined(__ANDROID__)
			touch_release_all(app);
#endif
			send_app_event_j1(app, EV_FOCUS_LOST, 0, 0);
		} break;
#if defined(__ANDROID__)
		case SDL_FINGERDOWN: touch_event_down(app, &e.tfinger); break;
		case SDL_FINGERMOTION: touch_event_motion(app, &e.tfinger); break;
		case SDL_FINGERUP: touch_event_up(app, &e.tfinger); break;
#endif
		case SDL_MOUSEMOTION: { int x, y; mouse_to_ui(e.motion.x, e.motion.y, &x, &y); send_app_event_j1(app, EV_MOUSE_MOVE, x, y); break; }
		case SDL_MOUSEBUTTONDOWN: if (e.button.button == SDL_BUTTON_LEFT) { int x, y; mouse_to_ui(e.button.x, e.button.y, &x, &y); send_app_event_j1(app, EV_MOUSE_DOWN, x, y); } break;
		default: break;
		}
	}
}

#if defined(__EMSCRIPTEN__)
/* En navegador se usa el event loop de Emscripten. El loop anterior dormía con
 * Asyncify después de cada present; junto con la presentación de SDL podía
 * sumar dos esperas por fotograma y vaciar la cola de audio. */
typedef struct {
	App* app;
	Uint32 max_bytes;
	int audio_rate;
	double freq;
	double last;
} WebLoopState;

static WebLoopState s_web_loop;

static void web_loop_cleanup(void) {
	debug_log_info("Saliendo del bucle web; liberando recursos");
	for (int i = 0; i < 16; i++) {
		if (P.pads[i]) {
			SDL_GameControllerClose(P.pads[i]);
			P.pads[i] = NULL;
		}
	}
	if (P.audio) { SDL_CloseAudioDevice(P.audio); P.audio = 0; }
	SDL_DestroyTexture(P.tex_ui); P.tex_ui = NULL;
	SDL_DestroyTexture(P.tex_game_sharp); P.tex_game_sharp = NULL;
	SDL_DestroyTexture(P.tex_game_smooth); P.tex_game_smooth = NULL;
	SDL_DestroyTexture(P.tex_game2_sharp); P.tex_game2_sharp = NULL;
	SDL_DestroyTexture(P.tex_game2_smooth); P.tex_game2_smooth = NULL;
	SDL_DestroyRenderer(P.ren); P.ren = NULL;
	SDL_DestroyWindow(P.win); P.win = NULL;
	SDL_Quit();
	debug_log_close();
}

enum {
	WEB_AUDIO_TARGET_MS = 50,
	WEB_AUDIO_PREROLL_MS = 10,
	WEB_AUDIO_MAX_CATCHUP_FRAMES = 4
};

/* Extrae y encola el audio recién generado. En web mantenemos un colchón corto
 * porque una sola cola de ~16 ms se agota si requestAnimationFrame llega tarde. */
static int web_queue_audio(WebLoopState* state) {
	if (!P.audio || !state->app) return 0;
	App* app = state->app;
	const int16_t* data = NULL;
	if (app_audio_reset_requested(app)) {
		static const char silence[1 << 15] = {0};
		SDL_ClearQueuedAudio(P.audio);
		Uint32 pre = (Uint32) state->audio_rate * 4 / 1000 * WEB_AUDIO_PREROLL_MS;
		if (pre > sizeof(silence)) pre = (Uint32) sizeof(silence);
		if (pre) (void) SDL_QueueAudio(P.audio, silence, pre);
	}
	int n = app_take_audio(app, &data);
	if (n > 0 && SDL_QueueAudio(P.audio, data, (Uint32) n * 4) != 0) {
		debug_log_warn("SDL_QueueAudio web falló: %s", SDL_GetError());
		return 0;
	}
	return n;
}

static void web_finish_loop(WebLoopState* state) {
	emscripten_cancel_main_loop();
	App* app = state->app;
	state->app = NULL;
	if (app) app_destroy(app);
	web_loop_cleanup();
}

static void web_loop_frame(void* userdata) {
	WebLoopState* state = (WebLoopState*) userdata;
	App* app = state->app;
	if (!app) {
		emscripten_cancel_main_loop();
		return;
	}
	pump_events(app);

	double now = (double) SDL_GetPerformanceCounter() / state->freq;
	double dt = now - state->last;
	state->last = now;
	if (dt < 0.0) dt = 0.0;
	if (dt > 0.1) dt = 0.1;

	if (P.audio) {
		Uint32 queued = SDL_GetQueuedAudioSize(P.audio);
		if (queued > state->max_bytes) {
			SDL_ClearQueuedAudio(P.audio);
			queued = 0;
		}
		app_audio_feedback(app, (int) (queued / 4));
	}

	if (!app_update(app, dt)) {
		web_finish_loop(state);
		return;
	}

	int produced = web_queue_audio(state);
	if (P.audio && produced > 0) {
		/* Si hubo un retraso puntual, adelantamos solo los fotogramas necesarios
		 * para recuperar 50 ms de audio. En régimen estable se ejecuta un frame
		 * por tick; no se duplica trabajo cuando la cola ya está llena. */
		const Uint32 target_bytes = (Uint32) state->audio_rate * 4 * WEB_AUDIO_TARGET_MS / 1000;
		for (int i = 0; i < WEB_AUDIO_MAX_CATCHUP_FRAMES; ++i) {
			if (SDL_GetQueuedAudioSize(P.audio) >= target_bytes) break;
			if (!app_update(app, 0.0)) {
				web_finish_loop(state);
				return;
			}
			produced = web_queue_audio(state);
			if (produced <= 0) break; /* menú/pausa: no simular fotogramas extra */
		}
	}

	AppFrame frame;
	app_render(app, &frame);
	present(app, &frame);
}
#endif

/* --------------------------------------------------------------------------- main */
#if defined(__ANDROID__)
#define APP_ENTRY SDL_main
#else
#define APP_ENTRY main
#endif
int APP_ENTRY(int argc, char** argv) {
	(void) argc; (void) argv;
#if defined(_WIN32)
	open_debug_console();
#endif
#if defined(__ORBIS__) || defined(PS4)
	/* /app0 es de solo lectura. Conserva el log y la configuración en almacenamiento escribible. */
	(void) mkdir("/data/SonicAdvanceTrilogy", 0777);
#endif
	debug_log_init();
	debug_log_info("Inicio de Sonic Advance Trilogy (modo SDL)");
	emu_enable_debug_logging();
	SDL_SetMainReady();
#if defined(__ORBIS__) || defined(PS4)
	/* OpenOrbis SDL todavía no garantiza un backend acelerado en todas las versiones.
	 * Consultar la salida real y utilizar el renderer software probado por el sample SDL2. */
	int ps4_screen_w = 1920, ps4_screen_h = 1080;
	int video_handle = sceVideoOutOpen(ORBIS_VIDEO_USER_MAIN, ORBIS_VIDEO_OUT_BUS_MAIN, 0, 0);
	if (video_handle >= 0) {
		OrbisVideoOutResolutionStatus video_resolution;
		memset(&video_resolution, 0, sizeof(video_resolution));
		if (sceVideoOutGetResolutionStatus(video_handle, &video_resolution) >= 0 &&
		    video_resolution.width > 0 && video_resolution.height > 0) {
			ps4_screen_w = (int) video_resolution.width;
			ps4_screen_h = (int) video_resolution.height;
		}
		(void) sceVideoOutClose(video_handle);
	}
	debug_log_info("PS4: resolución de salida %dx%d", ps4_screen_w, ps4_screen_h);
#endif
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) != 0) {
		debug_log_error("No se pudo iniciar SDL: %s", SDL_GetError());
		debug_log_close();
		return 1;
	}
	char base[1024] = ".";
#if defined(__EMSCRIPTEN__)
	/* Assets empacados por Emscripten viven en /assets. Los ROMs importados
	 * desde la página se copian a /main/roms, que es la ruta del launcher nativo. */
	snprintf(base, sizeof(base), ".");
#elif defined(__ANDROID__)
	/* Directorio privado de la app en almacenamiento externo: permite importar ROMs
	 * sin pedir permisos globales de almacenamiento (Android 10+). */
	const char* android_base = SDL_AndroidGetExternalStoragePath();
	if (!android_base || !*android_base) android_base = SDL_AndroidGetInternalStoragePath();
	if (android_base && *android_base) snprintf(base, sizeof(base), "%s", android_base);
#else
	char* bp = SDL_GetBasePath();
	if (bp) {
		snprintf(base, sizeof(base), "%s", bp);
		size_t n = strlen(base);
		while (n > 1 && (base[n - 1] == '/' || base[n - 1] == '\\')) base[--n] = 0;
		SDL_free(bp);
	} else {
#if defined(__ORBIS__) || defined(PS4)
		/* Dentro del PKG los recursos se leen desde app0 aunque el cwd sea distinto. */
		snprintf(base, sizeof(base), "/app0");
#endif
	}
#endif
#if defined(__ORBIS__) || defined(PS4)
	debug_log_info("PS4: base de recursos = %s", base);
	debug_log_info("PS4: ROMs esperadas en %s/main/roms", base);
	debug_log_info("PS4: assets esperados en %s/assets", base);
#endif

#if defined(__ANDROID__)
	SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
	P.win = SDL_CreateWindow("Sonic Advance Trilogy", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, SDL_WINDOW_FULLSCREEN | SDL_WINDOW_ALLOW_HIGHDPI);
#elif defined(__ORBIS__) || defined(PS4)
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
	/* El port SDL de OpenOrbis suele usar una ventana sin flags; el modo fullscreen
	 * genérico no está implementado del mismo modo en todas sus builds. */
	P.win = SDL_CreateWindow("Sonic Advance Trilogy", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, ps4_screen_w, ps4_screen_h, 0);
#else
	P.win = SDL_CreateWindow("Sonic Advance Trilogy Launcher", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
#endif
	if (!P.win) { debug_log_error("No se pudo crear la ventana: %s", SDL_GetError()); SDL_Quit(); debug_log_close(); return 1; }
#if defined(__ORBIS__) || defined(PS4)
	int ps4_window_w = 0, ps4_window_h = 0;
	SDL_GetWindowSize(P.win, &ps4_window_w, &ps4_window_h);
	debug_log_info("PS4: ventana creada a %dx%d; driver de vídeo SDL=%s", ps4_window_w, ps4_window_h,
	               SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "desconocido");
#endif
#if defined(__ANDROID__)
	/* Controlamos el ritmo a ~59,7 FPS nosotros mismos: dejar VSYNC activado
	 * en pantallas de 90/120 Hz puede provocar 45/40 FPS por la cadencia de swap. */
	SDL_SetHint(SDL_HINT_RENDER_VSYNC, "0");
	P.ren = SDL_CreateRenderer(P.win, -1, SDL_RENDERER_ACCELERATED);
	if (!P.ren) { debug_log_warn("Renderizador acelerado no disponible: %s", SDL_GetError()); P.ren = SDL_CreateRenderer(P.win, -1, SDL_RENDERER_SOFTWARE); }
#elif defined(__ORBIS__) || defined(PS4)
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
	/* Usar el patrón de presentación del sample SDL2 de OpenOrbis: renderizar
	 * sobre SDL_GetWindowSurface() y hacer flip con SDL_UpdateWindowSurface().
	 * SDL_RenderPresent() no garantiza la presentación en este port de Orbis. */
	P.window_surface = SDL_GetWindowSurface(P.win);
	if (!P.window_surface) {
		debug_log_error("PS4: SDL_GetWindowSurface falló: %s", SDL_GetError());
		SDL_DestroyWindow(P.win); SDL_Quit(); debug_log_close(); return 1;
	}
	debug_log_info("PS4: superficie SDL %dx%d pitch=%d formato=0x%08x",
	               P.window_surface->w, P.window_surface->h, P.window_surface->pitch,
	               (unsigned) P.window_surface->format->format);
	P.ren = SDL_CreateSoftwareRenderer(P.window_surface);
	if (!P.ren) {
		debug_log_error("PS4: SDL_CreateSoftwareRenderer falló: %s", SDL_GetError());
		SDL_DestroyWindow(P.win); SDL_Quit(); debug_log_close(); return 1;
	}
	debug_log_info("PS4: renderer software sobre surface; presentacion mediante SDL_UpdateWindowSurface");
#else
	SDL_SetHint(SDL_HINT_RENDER_VSYNC, "0"); /* el reloj del launcher fija los fotogramas */
	P.ren = SDL_CreateRenderer(P.win, -1, SDL_RENDERER_ACCELERATED);
	if (!P.ren) { debug_log_warn("Renderizador acelerado no disponible: %s", SDL_GetError()); P.ren = SDL_CreateRenderer(P.win, -1, SDL_RENDERER_SOFTWARE); }
#endif
	if (P.ren) {
		SDL_RendererInfo ri;
		if (SDL_GetRendererInfo(P.ren, &ri) == 0) {
			debug_log_info("Renderizador: %s (flags=0x%x, formatos=%u)", ri.name, (unsigned) ri.flags, (unsigned) ri.num_texture_formats);
#if defined(__ORBIS__) || defined(PS4)
			debug_log_info("PS4: renderer seleccionado=%s; flags=0x%x; formatos de textura=%u", ri.name,
			               (unsigned) ri.flags, (unsigned) ri.num_texture_formats);
			for (Uint32 fi = 0; fi < ri.num_texture_formats; ++fi)
				debug_log_info("PS4: texture_format[%u]=0x%08x", (unsigned) fi, (unsigned) ri.texture_formats[fi]);
#endif
		}
	}
	if (!P.ren) { debug_log_error("No se pudo crear el renderizador: %s", SDL_GetError()); SDL_DestroyWindow(P.win); SDL_Quit(); debug_log_close(); return 1; }
#if defined(__ORBIS__) || defined(PS4)
	/* Prueba visible antes de cargar los assets (que pueden tardar por el reescalado).
	 * Si esto tampoco aparece, el problema está en la salida SDL/renderer, no en mGBA. */
	debug_log_info("PS4: enviando fotograma de diagnóstico antes de cargar recursos");
	SDL_SetRenderDrawColor(P.ren, 18, 62, 130, 255);
	if (SDL_RenderClear(P.ren) != 0) debug_log_error("PS4: SDL_RenderClear de diagnóstico falló: %s", SDL_GetError());
	SDL_Rect boot_bar = { ps4_screen_w / 8, ps4_screen_h / 3, ps4_screen_w * 3 / 4, ps4_screen_h / 12 };
	SDL_SetRenderDrawColor(P.ren, 235, 190, 45, 255);
	if (SDL_RenderFillRect(P.ren, &boot_bar) != 0) debug_log_error("PS4: SDL_RenderFillRect de diagnóstico falló: %s", SDL_GetError());
	if (SDL_UpdateWindowSurface(P.win) != 0) {
		debug_log_error("PS4: SDL_UpdateWindowSurface del diagnóstico falló: %s", SDL_GetError());
	} else {
		debug_log_info("PS4: SDL_UpdateWindowSurface del diagnóstico devolvió OK");
	}
	SDL_Delay(250);
	debug_log_info("PS4: fotograma de diagnóstico enviado; creando texturas del launcher");
#endif
	P.tex_ui = make_tex(SDL_PIXELFORMAT_ARGB8888, UI_W, UI_H, 1, SDL_BLENDMODE_BLEND);
	P.tex_game_sharp = make_tex(SDL_PIXELFORMAT_ABGR8888, EMU_W, EMU_H, 0, SDL_BLENDMODE_NONE);
	P.tex_game_smooth = make_tex(SDL_PIXELFORMAT_ABGR8888, EMU_W, EMU_H, 1, SDL_BLENDMODE_NONE);
	P.tex_game2_sharp = make_tex(SDL_PIXELFORMAT_ABGR8888, EMU_W, EMU_H, 0, SDL_BLENDMODE_NONE);
	P.tex_game2_smooth = make_tex(SDL_PIXELFORMAT_ABGR8888, EMU_W, EMU_H, 1, SDL_BLENDMODE_NONE);
	if (!P.tex_ui || !P.tex_game_sharp || !P.tex_game_smooth || !P.tex_game2_sharp || !P.tex_game2_smooth) {
		debug_log_error("No se pudieron crear las texturas: %s (UI=%p GBAsharp=%p GBAsmooth=%p J2sharp=%p J2smooth=%p)", SDL_GetError(),
		                (void*)P.tex_ui, (void*)P.tex_game_sharp, (void*)P.tex_game_smooth,
		                (void*)P.tex_game2_sharp, (void*)P.tex_game2_smooth);
		return 1;
	}
#if defined(__ORBIS__) || defined(PS4)
	debug_log_info("PS4: texturas principales creadas correctamente (UI=%dx%d, GBA=%dx%d)", UI_W, UI_H, EMU_W, EMU_H);
#endif

	/* icono de la ventana (assets/icon.png) */
	{
		char ip[1100]; int iw, ih;
		snprintf(ip, sizeof(ip), "%s/assets/icon.png", base);
		uint32_t* px = png_load_file(ip, &iw, &ih);
		if (px) {
			SDL_Surface* s = SDL_CreateRGBSurfaceWithFormatFrom(px, iw, ih, 32, iw * 4, SDL_PIXELFORMAT_ARGB8888);
			if (s) { SDL_SetWindowIcon(P.win, s); SDL_FreeSurface(s); }
			free(px);
		}
	}

	/* Audio: se restaura la apertura con selección de controlador y el buffer de 40 ms. */
	AppHost host;
	memset(&host, 0, sizeof(host));
	static char video_info[160];
	SDL_RendererInfo host_ri;
	if (SDL_GetRendererInfo(P.ren, &host_ri) == 0) {
		snprintf(video_info, sizeof(video_info), "%s (%s)", host_ri.name,
		         (host_ri.flags & SDL_RENDERER_ACCELERATED) ? "acelerado" : "por software");
		host.video_info = video_info;
	}
	SDL_AudioSpec have;
	SDL_zero(have);
	P.audio = open_audio(&have);
	host.audio_rate = P.audio ? have.freq : 48000;
	host.has_audio = P.audio != 0;
	static char audio_info[160];
	if (P.audio) {
		const char* drv = SDL_GetCurrentAudioDriver();
		snprintf(audio_info, sizeof(audio_info), "%s, %d Hz, %d canales, buffer %d", drv ? drv : "?", (int) have.freq, (int) have.channels, (int) have.samples);
		host.audio_info = audio_info;
		debug_log_info("Audio listo: %s", audio_info);
		SDL_PauseAudioDevice(P.audio, 0);
	} else {
		debug_log_error("No se pudo abrir el dispositivo de audio: %s", SDL_GetError());
	}
	const Uint32 max_bytes = (Uint32) host.audio_rate * 4 / 1000 * AUDIO_MAX_MS;

	host.open_folder = h_open_folder; host.set_fullscreen = h_fullscreen; host.set_title = h_title;
	host.set_cursor_visible = h_cursor; host.key_name = h_key_name;
#if defined(__ORBIS__) || defined(PS4)
	/* /app0 es contenido instalado y no debe usarse para partidas/configuración. */
	static char ps4_user_data_base[] = "/data/SonicAdvanceTrilogy";
	host.user_data_dir = ps4_user_data_base;
#elif defined(__EMSCRIPTEN__)
	/* IDBFS persistirá partidas y configuración sin cambiar la lógica del launcher. */
	static char web_user_data_base[] = "/persistent";
	host.user_data_dir = web_user_data_base;
#endif
	for (int i = 0; i < SDL_NumJoysticks(); i++) open_pad(i);
	debug_log_info("Mandos reconocidos al iniciar: %d", opened_pad_count());

#if defined(__ORBIS__) || defined(PS4)
	debug_log_info("PS4: entrando en app_create; puede tardar procesando carátulas");
#endif
	App* app = app_create(base, &host);
	if (!app) { debug_log_error("No se pudo iniciar el launcher: app_create devolvió NULL"); if (P.audio) SDL_CloseAudioDevice(P.audio); SDL_Quit(); debug_log_close(); return 1; }
#if defined(__ORBIS__) || defined(PS4)
	debug_log_info("PS4: app_create terminó; se inicia el bucle de renderizado");
#endif
	send_app_event_j1(app, EV_PAD_COUNT, 0, opened_pad_count());

#if defined(__EMSCRIPTEN__)
	/* El loop web se programa por el navegador a 60 Hz. No bloquea el hilo con
	 * emscripten_sleep ni necesita Asyncify; el audio se rellena una vez por tick. */
	s_web_loop.app = app;
	s_web_loop.max_bytes = max_bytes;
	s_web_loop.audio_rate = host.audio_rate;
	s_web_loop.freq = (double) SDL_GetPerformanceFrequency();
	s_web_loop.last = (double) SDL_GetPerformanceCounter() / s_web_loop.freq;
	debug_log_info("WebAssembly: iniciando event loop de navegador a 60 Hz");
	emscripten_set_main_loop_arg(web_loop_frame, &s_web_loop, 60, 0);
	return 0;
#else
#if defined(__ORBIS__) || defined(PS4)
	/* El SDL de Orbis no expone SDL_GetPerformanceFrequency de forma uniforme en
	 * todas las builds. SDL_GetTicks sí es el reloj usado por el sample de OpenOrbis. */
	Uint32 ps4_last_tick = SDL_GetTicks();
	Uint32 ps4_next_tick = ps4_last_tick;
	double ps4_frame_fraction_ms = 0.0;
	Uint32 ps4_fps_start = ps4_last_tick;
	unsigned ps4_frame_count = 0;
#else
	const double freq = (double) SDL_GetPerformanceFrequency();
	double last = SDL_GetPerformanceCounter() / freq, next = last;
#endif
	AppFrame frame;
	for (;;) {
		pump_events(app);
#if defined(__ORBIS__) || defined(PS4)
		Uint32 tick_now = SDL_GetTicks();
		/* La resta unsigned gestiona el wrap de SDL_GetTicks cada ~49 días. */
		double dt = (double) (Uint32) (tick_now - ps4_last_tick) / 1000.0;
		ps4_last_tick = tick_now;
#else
		double now = SDL_GetPerformanceCounter() / freq;
		double dt = now - last;
		last = now;
#endif
		if (dt > 0.1) dt = 0.1;
		if (P.audio) {
			Uint32 queued = SDL_GetQueuedAudioSize(P.audio);
			if (queued > max_bytes) { SDL_ClearQueuedAudio(P.audio); queued = 0; }
			app_audio_feedback(app, (int) (queued / 4));
		}
		if (!app_update(app, dt)) break;
		app_render(app, &frame);
		present(app, &frame);
#if defined(__ORBIS__) || defined(PS4)
		ps4_frame_count++;
		Uint32 fps_elapsed = (Uint32) (SDL_GetTicks() - ps4_fps_start);
		if (fps_elapsed >= 5000) {
			debug_log_info("PS4: FPS del launcher %.1f; estado=%s; salida=%dx%d",
			               (double) ps4_frame_count * 1000.0 / (double) fps_elapsed,
			               app_is_paused(app) ? "pausa" : (frame.in_game ? "juego" : "hub"),
			               ps4_screen_w, ps4_screen_h);
			ps4_frame_count = 0;
			ps4_fps_start = SDL_GetTicks();
		}
#endif

		if (P.audio) {
			const int16_t* data;
			if (app_audio_reset_requested(app)) {
				static char silence[1 << 15];
				SDL_ClearQueuedAudio(P.audio);
				Uint32 pre = (Uint32) host.audio_rate * 4 / 1000 * AUDIO_PREROLL_MS;
				SDL_QueueAudio(P.audio, silence, pre < sizeof(silence) ? pre : (Uint32) sizeof(silence));
			}
			int n = app_take_audio(app, &data);
			if (n > 0) {
				if (SDL_QueueAudio(P.audio, data, (Uint32) n * 4) != 0)
					debug_log_warn("SDL_QueueAudio falló: %s", SDL_GetError());
			}
		}

#if defined(__ORBIS__) || defined(PS4)
		/* Ritmo fraccional sin SDL_GetPerformanceCounter ni espera activa: evita
		 * 100% CPU en un bucle que no presenta imagen si el contador no funciona. */
		ps4_frame_fraction_ms += app_frame_time(app) * 1000.0;
		Uint32 frame_ms = (Uint32) ps4_frame_fraction_ms;
		ps4_frame_fraction_ms -= (double) frame_ms;
		if (frame_ms == 0) frame_ms = 1;
		ps4_next_tick += frame_ms;
		int32_t delay_ms = (int32_t) (ps4_next_tick - SDL_GetTicks());
		if (delay_ms > 0) SDL_Delay((Uint32) delay_ms);
		else if (delay_ms < -250) {
			ps4_next_tick = SDL_GetTicks();
			ps4_frame_fraction_ms = 0.0;
		}
#else
		next += app_frame_time(app);
		double t = SDL_GetPerformanceCounter() / freq, delay = next - t;
#if defined(__EMSCRIPTEN__)
		/* Un bucle infinito no puede retener el hilo principal del navegador.
		 * Asyncify permite suspender aquí y devolver el control a eventos/repintado. */
		if (delay > 0.001) emscripten_sleep((unsigned int) (delay * 1000.0));
		else emscripten_sleep(0);
		if (delay < -0.25) next = SDL_GetPerformanceCounter() / freq;
#elif defined(__ANDROID__)
		/* Sin spin-wait en móviles: dormir casi todo el intervalo reduce consumo,
		 * pero el reloj fijo mantiene la emulación en torno a 59,7 FPS incluso
		 * en pantallas de 90/120 Hz. */
		if (delay > 0.002) SDL_Delay((Uint32)((delay - 0.001) * 1000.0));
		if (delay < -0.25) next = SDL_GetPerformanceCounter() / freq;
#else
		if (delay > 0.002) SDL_Delay((Uint32) ((delay - 0.001) * 1000.0));
		if (delay > -0.25) { while (SDL_GetPerformanceCounter() / freq < next) { } }
		else next = SDL_GetPerformanceCounter() / freq;
#endif
#endif
	}


	debug_log_info("El bucle principal terminó; liberando recursos");
	app_destroy(app);
	if (P.audio) SDL_CloseAudioDevice(P.audio);
	SDL_DestroyTexture(P.tex_ui); SDL_DestroyTexture(P.tex_game_sharp); SDL_DestroyTexture(P.tex_game_smooth);
	SDL_DestroyTexture(P.tex_game2_sharp); SDL_DestroyTexture(P.tex_game2_smooth);
	SDL_DestroyRenderer(P.ren);
	SDL_DestroyWindow(P.win);
	SDL_Quit();
	debug_log_close();
	return 0;
#endif
}
