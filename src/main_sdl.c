/* main_sdl.c - plataforma de escritorio (Windows / Linux / macOS) con SDL2.
 * Es la ÚNICA parte del launcher que depende de SDL: abre la ventana, el audio y los mandos,
 * traduce eventos a AppEvent y muestra lo que app_render() produce. */
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app.h"
#include "emu.h"
#include "keys.h"
#include "png.h"

#if defined(_WIN32)
#include <windows.h>
#include <shellapi.h>
#endif

typedef struct {
	SDL_Window* win;
	SDL_Renderer* ren;
	SDL_Texture *tex_ui, *tex_game_sharp, *tex_game_smooth;
	SDL_AudioDeviceID audio;
	SDL_GameController* pads[16];
	int fullscreen;
} Plat;

static Plat P;

/* ------------------------------------------------------------- servicios para la app */
static void h_open_folder(void* u, const char* path) {
	(void) u;
#if defined(_WIN32)
	ShellExecuteA(NULL, "open", path, NULL, NULL, SW_SHOWNORMAL);
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

static void present(const AppFrame* f) {
	int ow, oh;
	SDL_GetRendererOutputSize(P.ren, &ow, &oh);
	SDL_SetRenderDrawColor(P.ren, 0, 0, 0, 255);
	SDL_RenderClear(P.ren);
	if (f->in_game && f->game) {
		SDL_Texture* t = f->smooth ? P.tex_game_smooth : P.tex_game_sharp;
		SDL_UpdateTexture(t, NULL, f->game, EMU_W * 4);
		float s = (float) ow / EMU_W < (float) oh / EMU_H ? (float) ow / EMU_W : (float) oh / EMU_H;
		if (f->integer_scale && s >= 1.0f) s = (float) (int) s;
		SDL_Rect d;
		d.w = (int) (EMU_W * s); d.h = (int) (EMU_H * s);
		d.x = (ow - d.w) / 2; d.y = (oh - d.h) / 2;
		SDL_RenderCopy(P.ren, t, NULL, &d);
	}
	if (f->ui) {
		SDL_UpdateTexture(P.tex_ui, NULL, f->ui->px, UI_W * 4);
		SDL_Rect d;
		ui_dest(ow, oh, &d);
		SDL_RenderCopy(P.ren, P.tex_ui, NULL, &d);
	}
	SDL_RenderPresent(P.ren);
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
	return (int) k;
}

static void post_event(App* app, int type, int code, int value) { AppEvent e = {type, code, value}; app_event(app, &e); }

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
		case SDL_QUIT: post_event(app, EV_QUIT, 0, 0); break;
		case SDL_KEYDOWN: if (!e.key.repeat) post_event(app, EV_KEY_DOWN, map_key(e.key.keysym.sym), (e.key.keysym.mod & KMOD_ALT) ? 1 : 0); break;
		case SDL_KEYUP: post_event(app, EV_KEY_UP, map_key(e.key.keysym.sym), 0); break;
		case SDL_CONTROLLERDEVICEADDED: open_pad(e.cdevice.which); break;
		case SDL_CONTROLLERDEVICEREMOVED: close_pad(e.cdevice.which); break;
		case SDL_CONTROLLERBUTTONDOWN: post_event(app, EV_PAD_DOWN, e.cbutton.button, 0); break;
		case SDL_CONTROLLERBUTTONUP: post_event(app, EV_PAD_UP, e.cbutton.button, 0); break;
		case SDL_CONTROLLERAXISMOTION: post_event(app, EV_PAD_AXIS, e.caxis.axis, e.caxis.value); break;
		case SDL_WINDOWEVENT: if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST) post_event(app, EV_FOCUS_LOST, 0, 0); break;
		case SDL_MOUSEMOTION: { int x, y; mouse_to_ui(e.motion.x, e.motion.y, &x, &y); post_event(app, EV_MOUSE_MOVE, x, y); break; }
		case SDL_MOUSEBUTTONDOWN: if (e.button.button == SDL_BUTTON_LEFT) { int x, y; mouse_to_ui(e.button.x, e.button.y, &x, &y); post_event(app, EV_MOUSE_DOWN, x, y); } break;
		default: break;
		}
	}
}

/* --------------------------------------------------------------------------- main */
int main(int argc, char** argv) {
	(void) argc; (void) argv;
	SDL_SetMainReady();
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
		fprintf(stderr, "No se pudo iniciar SDL: %s\n", SDL_GetError());
		return 1;
	}
	char base[1024] = ".";
	char* bp = SDL_GetBasePath();
	if (bp) {
		snprintf(base, sizeof(base), "%s", bp);
		size_t n = strlen(base);
		while (n > 1 && (base[n - 1] == '/' || base[n - 1] == '\\')) base[--n] = 0;
		SDL_free(bp);
	}

	P.win = SDL_CreateWindow("Sonic Advance Trilogy Launcher", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
	if (!P.win) { fprintf(stderr, "No se pudo crear la ventana: %s\n", SDL_GetError()); return 1; }
	P.ren = SDL_CreateRenderer(P.win, -1, SDL_RENDERER_ACCELERATED);
	if (!P.ren) P.ren = SDL_CreateRenderer(P.win, -1, SDL_RENDERER_SOFTWARE);
	if (!P.ren) { fprintf(stderr, "No se pudo crear el renderizador: %s\n", SDL_GetError()); return 1; }
	P.tex_ui = make_tex(SDL_PIXELFORMAT_ARGB8888, UI_W, UI_H, 1, SDL_BLENDMODE_BLEND);
	P.tex_game_sharp = make_tex(SDL_PIXELFORMAT_ABGR8888, EMU_W, EMU_H, 0, SDL_BLENDMODE_NONE);
	P.tex_game_smooth = make_tex(SDL_PIXELFORMAT_ABGR8888, EMU_W, EMU_H, 1, SDL_BLENDMODE_NONE);
	if (!P.tex_ui || !P.tex_game_sharp || !P.tex_game_smooth) { fprintf(stderr, "No se pudieron crear las texturas: %s\n", SDL_GetError()); return 1; }

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

	/* audio */
	AppHost host;
	memset(&host, 0, sizeof(host));
	SDL_AudioSpec want, have;
	SDL_zero(want);
	want.freq = 48000; want.format = AUDIO_S16SYS; want.channels = 2; want.samples = 1024;
	P.audio = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
	host.audio_rate = P.audio ? have.freq : 48000;
	host.has_audio = P.audio != 0;
	if (P.audio) SDL_PauseAudioDevice(P.audio, 0);

	host.open_folder = h_open_folder; host.set_fullscreen = h_fullscreen; host.set_title = h_title;
	host.set_cursor_visible = h_cursor; host.key_name = h_key_name;
	for (int i = 0; i < SDL_NumJoysticks(); i++) open_pad(i);

	App* app = app_create(base, &host);
	if (!app) { fprintf(stderr, "No se pudo iniciar el launcher\n"); return 1; }

	const double freq = (double) SDL_GetPerformanceFrequency();
	double last = SDL_GetPerformanceCounter() / freq, next = last;
	AppFrame frame;
	for (;;) {
		pump_events(app);
		double now = SDL_GetPerformanceCounter() / freq;
		double dt = now - last;
		last = now;
		if (dt > 0.1) dt = 0.1;
		if (!app_update(app, dt)) break;
		app_render(app, &frame);
		present(&frame);

		if (P.audio) {
			const int16_t* data;
			int n = app_take_audio(app, &data);
			if (app_audio_reset_requested(app)) SDL_ClearQueuedAudio(P.audio);
			if (n > 0 && SDL_GetQueuedAudioSize(P.audio) < (Uint32) (host.audio_rate / 7 * 4))   /* máx. ~140 ms en cola */
				SDL_QueueAudio(P.audio, data, (Uint32) n * 4);
		}

		next += app_frame_time(app);
		double t = SDL_GetPerformanceCounter() / freq, delay = next - t;
		if (delay > 0.002) SDL_Delay((Uint32) ((delay - 0.001) * 1000.0));
		if (delay > -0.25) { while (SDL_GetPerformanceCounter() / freq < next) { } }
		else next = SDL_GetPerformanceCounter() / freq;
	}

	app_destroy(app);
	if (P.audio) SDL_CloseAudioDevice(P.audio);
	SDL_DestroyTexture(P.tex_ui); SDL_DestroyTexture(P.tex_game_sharp); SDL_DestroyTexture(P.tex_game_smooth);
	SDL_DestroyRenderer(P.ren);
	SDL_DestroyWindow(P.win);
	SDL_Quit();
	return 0;
}
