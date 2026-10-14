/* Prueba sin ventana: ejecuta el launcher completo con el mGBA real y una ROM mínima,
 * comprueba el flujo (menús, pausa, ranuras, remapeo, mando) y guarda capturas PNG. */
#include "../src/app_internal.h"
#include "../src/png.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures, checks;
#define CHECK(cond, msg) do { checks++; if (!(cond)) { failures++; printf("  FALLO: %s (línea %d)\n", msg, __LINE__); } } while (0)

static int fullscreen_calls, folder_calls, last_fs;
static char last_title[160];
static void h_open(void* u, const char* p) { (void) u; (void) p; folder_calls++; }
static void h_fs(void* u, int on) { (void) u; fullscreen_calls++; last_fs = on; }
static void h_title(void* u, const char* t) { (void) u; snprintf(last_title, sizeof(last_title), "%s", t); }
static void h_cursor(void* u, int v) { (void) u; (void) v; }

static App* app;
static const char* shotdir = NULL;     /* con argumento 2: guarda capturas PNG */
static char work[512];

static void ev(int type, int code, int value) { AppEvent e = {type, code, value, 0}; app_event(app, &e); }
static void key(int k) { ev(EV_KEY_DOWN, k, 0); ev(EV_KEY_UP, k, 0); }
static void pad(int b) { ev(EV_PAD_DOWN, b, 0); ev(EV_PAD_UP, b, 0); }
static void step(int n) { for (int i = 0; i < n; i++) app_update(app, 1.0 / 60.0); }
static int audio_total;
static void stepg(int n) {
	for (int i = 0; i < n; i++) {
		app_update(app, 1.0 / 59.7275);
		const int16_t* d;
		audio_total += app_take_audio(app, &d);
	}
}

static Canvas* shot_cv;
static void shot(const char* name) {
	if (!shotdir) return;
	AppFrame f;
	app_render(app, &f);
	if (!shot_cv) shot_cv = cv_new(UI_W, UI_H);
	if (f.in_game) {
		cv_clear(shot_cv, 0xFF000000);
		if (f.game) {
			int w = 1620, h = 1080, x0 = (UI_W - w) / 2;
			for (int y = 0; y < h; y++)
				for (int x = 0; x < w; x++) {
					uint32_t v = f.game[(y * EMU_H / h) * EMU_W + (x * EMU_W / w)];
					shot_cv->px[y * UI_W + x0 + x] = 0xFF000000u | ((v & 255) << 16) | (v & 0xFF00) | ((v >> 16) & 255);
				}
		}
		if (f.ui) gfx_blit(shot_cv, f.ui, 0, 0, 255);
	} else memcpy(shot_cv->px, f.ui->px, (size_t) UI_W * UI_H * 4);
	char p[256];
	snprintf(p, sizeof(p), "%s/%s.png", shotdir, name);
	png_save_file(p, shot_cv->px, UI_W, UI_H, UI_W, 0);
}

/* ROM mínima de GBA escrita a mano en ARM (rellena la pantalla en modo 3): sin material con copyright. */
static void write_test_rom(const char* path) {
	static const uint32_t code[] = {0xE3A00301, 0xE3A01B01, 0xE3811003, 0xE5801000, 0xE3A02406, 0xE3A0301F,
	                                0xE3A05C96, 0xE0C230B2, 0xE2555001, 0x1AFFFFFC, 0xEAFFFFFE};
	uint8_t rom[0x1000];
	memset(rom, 0, sizeof(rom));
	rom[0] = 0x2E; rom[3] = 0xEA;
	memcpy(rom + 0xA0, "TESTROM", 7);
	memcpy(rom + 0xAC, "ATST", 4);
	memcpy(rom + 0xB0, "01", 2);
	rom[0xB2] = 0x96;
	int chk = 0;
	for (int i = 0xA0; i < 0xBD; i++) chk -= rom[i];
	rom[0xBD] = (uint8_t) (chk - 0x19);
	for (size_t i = 0; i < sizeof(code) / 4; i++) for (int b = 0; b < 4; b++) rom[0xC0 + i * 4 + b] = (uint8_t) (code[i] >> (8 * b));
	FILE* f = fopen(path, "wb");
	if (f) { fwrite(rom, 1, sizeof(rom), f); fclose(f); }
}

int main(int argc, char** argv) {
	snprintf(work, sizeof(work), "%s", argc > 1 ? argv[1] : "ht_work");
	if (argc > 2) { shotdir = argv[2]; sys_mkdir_p(shotdir); }
	char path[700];
	snprintf(path, sizeof(path), "%s/main/roms", work); sys_mkdir_p(path);
	snprintf(path, sizeof(path), "%s/main/roms/SonicAdvance.GBA", work); write_test_rom(path);
	snprintf(path, sizeof(path), "%s/main/config.txt", work); sys_remove(path);
	for (int n = 1; n <= SLOT_COUNT; n++) {
		snprintf(path, sizeof(path), "%s/main/saves/SonicAdvance.ss%d", work, n); sys_remove(path);
		strcat(path, ".png"); sys_remove(path);
	}
	AppHost host = {0};
	host.open_folder = h_open; host.set_fullscreen = h_fs; host.set_title = h_title; host.set_cursor_visible = h_cursor;
	host.audio_rate = 48000; host.has_audio = 1;
	app = app_create(work, &host);
	CHECK(app != NULL, "app_create");
	printf("mGBA %s | roms: %d %d %d\n", emu_version(), app->rom_found[0], app->rom_found[1], app->rom_found[2]);
	CHECK(app->rom_found[0] && !app->rom_found[1] && !app->rom_found[2], "detección de ROMs (sin distinguir mayúsculas)");

	step(30); shot("01_principal");
	key(K_RIGHT); step(30); shot("02_principal_sin_rom");
	key(K_RETURN); step(1);
	CHECK(app->state == ST_MAIN && app->toast[0], "jugar sin ROM muestra aviso y no inicia");
	key(K_LEFT); step(30);

	/* ---- iniciar juego */
	key(K_RETURN); step(1);
	CHECK(app->loading == 1, "pantalla 'Iniciando…'");
	step(1);
	CHECK(app->state == ST_GAME && app->emu, "el juego arranca dentro del launcher");
	CHECK(strstr(last_title, "Sonic Advance") != NULL, "título de ventana");
	stepg(40);
	CHECK(app->have_frame, "hay fotograma");
	CHECK(audio_total > 40 * 700 && audio_total < 40 * 900, "audio ~803 muestras por fotograma");
	shot("03_juego");

	/* ---- botones y turbo */
	ev(EV_KEY_DOWN, 'x', 0); ev(EV_KEY_DOWN, K_TAB, 0); stepg(1);
	CHECK(app->turbo, "turbo con Tab");
	ev(EV_KEY_UP, 'x', 0); ev(EV_KEY_UP, K_TAB, 0); stepg(1);
	CHECK(!app->turbo, "turbo se suelta");
	app->cfg.show_fps = 1; stepg(70); shot("04_juego_fps"); app->cfg.show_fps = 0;

	/* ---- pausa con Esc */
	key(K_ESCAPE); stepg(1);
	CHECK(app->state == ST_PAUSE, "Esc pausa el juego");
	step(30); shot("05_pausa");
	for (int i = 0; i < 3; i++) key(K_DOWN);
	step(30);
	int v0 = app->cfg.volume;
	key(K_RIGHT); CHECK(app->cfg.volume == v0 + 10, "volumen +10");
	key(K_LEFT); key(K_LEFT); CHECK(app->cfg.volume == v0 - 10, "volumen -10");
	Rect volume_row = ui_pause_rect(3);
	int volume_total = 10 * 20 + 9 * 6;
	int volume_x = volume_row.x + (volume_row.w + 28 - 10) - volume_total - 130;
	int volume_y = volume_row.y + volume_row.h / 2;
	CHECK(app_pause_volume_arrow_hit(app, volume_x - 24, volume_y) == -10, "flecha izquierda del volumen detectada");
	CHECK(app_pause_volume_arrow_hit(app, volume_x + volume_total + 24, volume_y) == 10, "flecha derecha del volumen detectada");
	int arrow_v = app->cfg.volume;
	ev(EV_TOUCH_VOLUME, 0, -10); CHECK(app->cfg.volume == arrow_v - 10, "flecha táctil baja volumen");
	ev(EV_TOUCH_VOLUME, 0, 10); CHECK(app->cfg.volume == arrow_v, "flecha táctil sube volumen");
	/* En pausa, palanca = navegación y A/B = confirmar/volver. */
	app->pause_sel = 0;
	ev(EV_TOUCH_DOWN, ACT_DOWN, 0); ev(EV_TOUCH_UP, ACT_DOWN, 0);
	CHECK(app->pause_sel == 1, "palanca táctil mueve selección de pausa");
	ev(EV_TOUCH_DOWN, ACT_B, 0); CHECK(app->state == ST_GAME, "B táctil vuelve al juego desde pausa");
	ev(EV_TOUCH_DOWN, ACT_PAUSE, 0); CHECK(app->state == ST_PAUSE, "pausa táctil vuelve a abrir el menú");
	app->pause_sel = 1;
	ev(EV_TOUCH_DOWN, ACT_A, 0); CHECK(app->state == ST_SLOTS, "A táctil confirma una opción del menú de pausa");
	ev(EV_TOUCH_DOWN, ACT_B, 0); CHECK(app->state == ST_PAUSE, "B táctil vuelve de ranuras a pausa");
	app->pause_sel = 3; /* deja el flujo posterior en la misma selección que el test original */
	step(20); shot("06_pausa_volumen");
	key(K_DOWN); fullscreen_calls = 0; key(K_RETURN);
	CHECK(app->cfg.fullscreen == 1 && fullscreen_calls == 1 && last_fs == 1, "pantalla completa desde la pausa");
	key(K_RETURN); CHECK(app->cfg.fullscreen == 0 && last_fs == 0, "salir de pantalla completa");

	/* ---- guardar en la ranura 3 */
	app->pause_sel = 1; key(K_RETURN);
	CHECK(app->state == ST_SLOTS && app->slot_mode == 0, "menú de guardado");
	key(K_RIGHT); key(K_RIGHT); key(K_RETURN); step(2);
	char p[700];
	slot_path(app, 3, p, sizeof(p));
	CHECK(sys_file_exists(p), "estado guardado en la ranura 3");
	strcat(p, ".png"); CHECK(sys_file_exists(p), "miniatura de la ranura 3");
	stepg(1); key(K_DOWN); key(K_LEFT); key(K_RETURN); step(2); /* ranura 6 */
	step(10); shot("07_ranuras_guardar");
	slot_path(app, 6, p, sizeof(p)); CHECK(sys_file_exists(p), "estado guardado en la ranura 6");
	key(K_DELETE); CHECK(sys_file_exists(p), "borrar pide confirmación");
	key(K_DELETE); CHECK(!sys_file_exists(p), "borrar con doble pulsación");
	key(K_ESCAPE); CHECK(app->state == ST_PAUSE, "Esc vuelve a la pausa");

	/* ---- cargar */
	app->pause_sel = 2; key(K_RETURN);
	CHECK(app->state == ST_SLOTS && app->slot_mode == 1 && app->slot_sel == 2, "cargar abre en la primera ranura ocupada");
	step(10); shot("08_ranuras_cargar");
	key(K_RETURN); CHECK(app->state == ST_GAME, "cargar vuelve al juego");
	stepg(5);

	/* ---- controles desde la pausa */
	key(K_ESCAPE); stepg(1); step(1);
	app->pause_sel = 5; key(K_RETURN);
	CHECK(app->state == ST_SETTINGS && app->settings_page == 1, "controles desde la pausa");
	step(5); shot("09_controles");
	key(K_RETURN); CHECK(app->capture_kind == 0, "esperando tecla");
	step(5); shot("10_controles_captura");
	key('k'); CHECK(app->cfg.keys[ACT_A] == 'k' && app->capture_kind == -1, "A reasignado a K");
	int oldB = app->cfg.keys[ACT_B];
	key(K_DOWN); key(K_RETURN); key('k');
	CHECK(app->cfg.keys[ACT_B] == 'k' && app->cfg.keys[ACT_A] == oldB, "intercambio al repetir tecla");
	app->ctrl_row = 0; key(K_RIGHT); key(K_RETURN); CHECK(app->capture_kind == 1, "esperando botón de mando");
	ev(EV_PAD_DOWN, 7, 0); ev(EV_PAD_UP, 7, 0);
	CHECK(app->cfg.pad[ACT_A] == 7 && app->capture_kind == -1, "A del mando = L3");
	key(K_RETURN); key(K_ESCAPE); CHECK(app->capture_kind == -1 && app->cfg.pad[ACT_A] == 7, "Esc cancela");
	key(K_DELETE); CHECK(app->cfg.pad[ACT_A] == -1, "Supr borra");
	app->ctrl_row = ACT_COUNT; key(K_RETURN);
	CHECK(app->cfg.keys[ACT_A] == 'x' && app->cfg.pad[ACT_A] == PAD_CIRCLE, "restablecer");
	app->ctrl_row = ACT_COUNT + 1; key(K_RETURN); CHECK(app->state == ST_PAUSE, "volver a la pausa");
	/* configuración persistida */
	Config c2; config_defaults(&c2); config_load(&c2, app->cfg_path);
	CHECK(c2.keys[ACT_A] == 'x' && c2.volume == app->cfg.volume, "config.txt guardado");

	/* ---- mando PS4 */
	pad(PAD_PS); CHECK(app->state == ST_GAME, "botón PS reanuda");
	stepg(2); pad(PAD_PS); CHECK(app->state == ST_PAUSE, "botón PS pausa");
	pad(PAD_CIRCLE); CHECK(app->state == ST_GAME, "círculo continúa");
	ev(EV_PAD_DOWN, PAD_CIRCLE, 0); ev(EV_PAD_DOWN, PAD_LEFT, 0); stepg(1);
	ev(EV_PAD_UP, PAD_CIRCLE, 0); ev(EV_PAD_UP, PAD_LEFT, 0);
	ev(EV_PAD_AXIS, 5, 30000); stepg(1); CHECK(app->turbo, "R2 = turbo");
	ev(EV_PAD_AXIS, 5, 0); stepg(1); CHECK(!app->turbo, "R2 suelto");
	ev(EV_FOCUS_LOST, 0, 0); CHECK(app->state == ST_PAUSE, "pérdida de foco = pausa");
	pad(PAD_PS); stepg(2);
	key(K_F5); CHECK(strstr(app->toast, "Guardado rápido") != NULL, "guardado rápido");
	key(K_F8); CHECK(strstr(app->toast, "Carga rápida") != NULL, "carga rápida");

	/* ---- reiniciar y salir */
	key(K_ESCAPE); step(1); app->pause_sel = 6; key(K_RETURN); CHECK(app->state == ST_GAME, "reiniciar");
	key(K_ESCAPE); step(1); app->pause_sel = 7; key(K_RETURN);
	CHECK(app->state == ST_MAIN && !app->emu, "salir al launcher");
	step(10);

	/* ---- aviso opcional y flujo real de pantalla dividida */
	ev(EV_PAD_COUNT, 0, 2);
	key(K_RETURN); step(1); CHECK(app->loading, "inicio con dos mandos muestra primero la carga");
	step(1);
	CHECK(app->modal && app->modal_kind == 1 && strstr(app->modal_text, "¿quieres jugar a pantalla dividida?") != NULL,
	      "aviso de pantalla dividida al detectar segundo mando");
	key(K_RIGHT); key(K_RETURN);
	CHECK(app->state == ST_GAME && app->emu && !app->emu2, "No inicia en un jugador");
	key(K_ESCAPE); step(1); app->pause_sel = 7; key(K_RETURN);
	CHECK(app->state == ST_MAIN && !app->emu, "salida del modo individual de prueba");
	key(K_RETURN); step(1); step(1);
	CHECK(app->modal && app->modal_kind == 1, "aviso vuelve a aparecer para la siguiente partida");
	key(K_RETURN);
	CHECK(app->state == ST_GAME && app->emu && app->emu2, "Sí inicia dos núcleos mGBA enlazados");
	int pair_audio_before = audio_total;
	stepg(8);
	AppFrame split_frame; app_render(app, &split_frame);
	CHECK(split_frame.in_game && split_frame.multiplayer && split_frame.game && split_frame.game2,
	      "pantalla dividida entrega fotogramas independientes para J1 y J2");
	CHECK(audio_total > pair_audio_before, "el audio del modo enlazado sigue llegando desde J1");
	key(K_F5); CHECK(strstr(app->toast, "Guardado rápido") != NULL, "guardado rápido enlazado");
	char pair_state[700]; slot_path(app, app->last_slot, pair_state, sizeof(pair_state));
	CHECK(sys_file_exists(pair_state), "estado J1 guardado en multijugador");
	char pair_state2[720]; snprintf(pair_state2, sizeof(pair_state2), "%s.p2", pair_state);
	CHECK(sys_file_exists(pair_state2), "estado J2 guardado junto al de J1");
	key(K_F8); CHECK(strstr(app->toast, "Carga rápida") != NULL, "carga rápida de ambos jugadores");
	stepg(2);
	key(K_ESCAPE); step(1); app->pause_sel = 7; key(K_RETURN);
	CHECK(app->state == ST_MAIN && !app->emu && !app->emu2, "salida limpia del modo multijugador");
	ev(EV_PAD_COUNT, 0, 1);

	/* ---- ajustes del launcher */
	key(K_TAB); step(5); CHECK(app->state == ST_SETTINGS, "ajustes");
	shot("11_ajustes");
	key(K_ESCAPE); CHECK(app->state == ST_MAIN, "volver de ajustes");
	/* ratón */
	ev(EV_MOUSE_MOVE, ui_card_rect(app, 2).x + 20, ui_card_rect(app, 2).y + 20); CHECK(app->sel == 2, "ratón: hover");
	ev(EV_MOUSE_MOVE, ui_btn_rect(3).x + 5, ui_btn_rect(3).y + 5); CHECK(app->zone == 1 && app->btn == 3, "ratón: botón");
	ev(EV_MOUSE_DOWN, ui_btn_rect(3).x + 5, ui_btn_rect(3).y + 5); CHECK(app->quit, "ratón: SALIR");
	CHECK(!app_update(app, 0.016), "app_update devuelve 0 al salir");

	app_destroy(app);
	printf("%d comprobaciones, %d fallos\n", checks, failures);
	return failures ? 1 : 0;
}
