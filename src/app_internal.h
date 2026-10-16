#ifndef APP_INTERNAL_H
#define APP_INTERNAL_H
#include "app.h"
#include "config.h"
#include "emu.h"
#include "keys.h"
#include "sys.h"
#if defined(__EMSCRIPTEN__)
#include <pthread.h>
#include <stdatomic.h>
#endif

#define NUM_GAMES 4
#define SLOT_COUNT 8
#define PAUSE_COUNT 8
#define SET_COUNT 8
#define PATH_MAX_APP 600
#define GBA_FPS 59.7275
#define TURBO_FRAMES 4
#define MAXSET 24

typedef struct {
	const char* id; const char* title; const char* rom; const char* art; const char* year;
	const char* tagline; const char* dev; const char* pub; const char* genre; const char* summary;
	uint32_t accent;
} GameInfo;
extern const GameInfo GAMES[NUM_GAMES];

typedef enum { ST_MAIN, ST_SETTINGS, ST_PAUSE, ST_SLOTS, ST_GAME } State;
enum { A_UP, A_DOWN, A_LEFT, A_RIGHT, A_ACCEPT, A_BACK, A_MENU, A_EXTRA, A_PREV, A_NEXT, A_NONE };

struct App {
	AppHost host;
	char base[PATH_MAX_APP], roms_dir[PATH_MAX_APP], saves_dir[PATH_MAX_APP], bios_file[PATH_MAX_APP];
	char cfg_path[PATH_MAX_APP], assets_dir[PATH_MAX_APP], games_art_dir[PATH_MAX_APP];
	Config cfg;
	double now;                      /* tiempo acumulado (s) */
	int quit;

	/* navegación */
	State state;
	int zone, sel, btn;              /* principal: zona 0 tarjetas / 1 botones */
	int settings_page;               /* 0 ajustes, 1 controles */
	int set_sel, controls_from_pause, ctrl_row, ctrl_col;
	int capture_kind, capture_act;   /* -1 ninguno, 0 teclado, 1 mando J1, 2 mando J2 */
	int modal_kind, modal_choice, prompt_game; /* 1 = selector de modo 1J/2J */
	int pause_sel; float pause_anim[PAUSE_COUNT];
	int slot_mode, slot_sel, last_slot; /* slot_mode: 0 guardar, 1 cargar */
	Canvas* slot_thumbs[SLOT_COUNT + 1];
	int delete_slot; double delete_t;
	int modal; char modal_title[96], modal_text[512];
	char toast[200]; double toast_until;
	int pending_launch, loading;
#if defined(__EMSCRIPTEN__)
	/* El arranque del cable link se ejecuta fuera del hilo que dibuja el navegador.
	 * La bandera atómica publica los punteros de salida una vez inicializados. */
	pthread_t web_launch_thread;
	atomic_int web_launch_done;
	int web_launch_active, web_launch_ok, web_launch_game;
	Emu* web_launch_emu; Emu* web_launch_emu2;
	char web_launch_rom[PATH_MAX_APP], web_launch_save[PATH_MAX_APP];
	char web_launch_save2[PATH_MAX_APP], web_launch_bios[PATH_MAX_APP];
	unsigned web_launch_audio_rate;
#endif
	float anim[NUM_GAMES];
	char rom_path[NUM_GAMES][PATH_MAX_APP]; int rom_found[NUM_GAMES]; double last_scan;

	/* entrada */
	int input_pad;                   /* 1 si el último dispositivo fue un mando */
	int held_dir[4]; double held_t[4];
	float stick[2]; int stick_dir;
	int trig[2];
	int keys_down[MAXSET], nkeys, pad_down[MAXSET], npad, stick_down[4], nstick;
	int pad_count;
	int p2_pad_down[MAXSET], p2_npad, p2_stick_down[4], p2_nstick;
	float p2_stick[2]; int p2_trig[2];
	int touch_down[ACT_COUNT];       /* botones virtuales Android en términos de acción GBA */

	/* juego */
	Emu* emu; Emu* emu2; int game_idx;
	uint32_t frame[EMU_W * EMU_H], frame2[EMU_W * EMU_H]; int have_frame;
	int16_t audio[16384 * 2]; int audio_frames; int16_t audio_out[16384 * 2];
	int audio_reset; int turbo;
	double audio_ratio;              /* ajuste suave de la frecuencia de audio */
	double audio_fill_avg;           /* media filtrada de la cola de audio */
	double fps, fps_acc; int fps_n;

	/* recursos */
	Canvas *icon, *title, *bg_base, *ui;
	Canvas *cards[NUM_GAMES], *cards_dark[NUM_GAMES], *backdrops[NUM_GAMES];
	Canvas* battle_slot; /* tarjeta visual reservada para Sonic Battle */
};

typedef struct { int kind; const char* label; int field; } SettingItem;   /* kind: 0 interruptor, 1 deslizador, 2 ir a, 3 acción, 4 volver */
extern const SettingItem SETTINGS[SET_COUNT];
extern const char* const PAUSE_LABELS[PAUSE_COUNT];   /* 0 continuar 1 guardar 2 cargar 3 volumen 4 pantalla completa 5 controles 6 reiniciar 7 salir */
int* cfg_field(Config* c, int field);                 /* 0 pantalla completa, 1 volumen, 2 suavizado, 3 escalado entero, 4 FPS */

/* app_logic.c */
void app_load_config(App* a);
void app_save_config(App* a);
void app_scan_roms(App* a);
void app_toast(App* a, const char* fmt, ...);
void app_modal(App* a, const char* title, const char* text);
void key_label(const App* a, int code, char* out, size_t n);
void pad_label(int code, char* out, size_t n);
int slot_exists(const App* a, int n);
void slot_path(const App* a, int n, char* out, size_t sz);
void app_load_thumbs(App* a);

/* app_ui.c */
void ui_load_assets(App* a);
void ui_free_assets(App* a);
void ui_draw(App* a);
/* geometría (compartida con la lógica de ratón) */
Rect ui_card_rect(const App* a, int i);
Rect ui_battle_slot_rect(void);
Rect ui_btn_rect(int j);
Rect ui_settings_row(int k);
Rect ui_ctrl_row_rect(int k);
Rect ui_ctrl_cell_rect(int k, int col);
Rect ui_pause_rect(int i);
Rect ui_slot_rect(int n);
Rect ui_modal_ok(void);
Rect ui_modal_yes(void);
Rect ui_modal_no(void);

#endif
