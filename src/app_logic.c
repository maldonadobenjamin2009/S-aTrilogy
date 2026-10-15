/* app_logic.c - estado, entrada, menús, emulación, ranuras de guardado (portable). */
#include "app_internal.h"
#include "debug_log.h"
#include "png.h"
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

const GameInfo GAMES[NUM_GAMES] = {
	{"sa1", "Sonic Advance", "sonicadvance.gba", "sonicadvance", "2001",
	 "El debut de Sonic en una consola Nintendo", "Dimps / Sonic Team", "Sega / THQ", "Plataformas 2D",
	 "Sonic Advance llegó a Game Boy Advance a finales de 2001 y fue el primer Sonic lanzado en una consola de Nintendo. "
	 "Recupera la velocidad clásica de Mega Drive en zonas de scroll lateral llenas de loops, resortes y jefes finales. "
	 "Elige entre Sonic, Tails, Knuckles o Amy, cada uno con sus propias habilidades, y reúne las siete Esmeraldas del "
	 "Caos para plantar cara al Dr. Eggman.",
	 0xFF288CFF},
	{"sa2", "Sonic Advance 2", "sonicadvance2.gba", "sonicadvance2", "2002",
	 "Más rápido, más grande, más espectacular", "Dimps / Sonic Team", "Sega / THQ", "Plataformas 2D",
	 "Publicado en 2002, Sonic Advance 2 apuesta todo a la velocidad: niveles enormes con rampas, loops y rutas alternativas "
	 "para correr sin parar. Juegas con Sonic, Tails, Knuckles y Cream, y puedes buscar los Special Rings escondidos para "
	 "llegar a las Esmeraldas del Caos y enfrentarte a las zonas finales contra el Dr. Eggman.",
	 0xFFFFB914},
	{"sa3", "Sonic Advance 3", "sonicadvance3.gba", "sonicadvance3", "2004",
	 "Dos héroes, una aventura", "Dimps / Sonic Team", "Sega / THQ", "Plataformas 2D",
	 "Lanzado en 2004, Sonic Advance 3 introduce el sistema de equipo: eliges un personaje principal y un compañero entre "
	 "Sonic, Tails, Knuckles, Amy y Cream, y cada pareja ofrece movimientos y habilidades distintos. Recorre zonas temáticas "
	 "desde el centro de Route 99, encuentra las Esmeraldas del Caos y frena de nuevo los planes del Dr. Eggman.",
	 0xFFEB465A},
};

const SettingItem SETTINGS[SET_COUNT] = {
	{0, "Pantalla completa", 0}, {1, "Volumen", 1}, {0, "Suavizado de imagen", 2},
	{0, "Escalado entero (píxeles nítidos)", 3}, {0, "Mostrar FPS durante el juego", 4},
	{2, "Controles…", 0}, {3, "Abrir carpeta de ROMs", 0}, {4, "Volver", 0},
};
const char* const PAUSE_LABELS[PAUSE_COUNT] = {
	"CONTINUAR", "GUARDAR PARTIDA", "CARGAR PARTIDA", "VOLUMEN", "PANTALLA COMPLETA", "CONTROLES", "REINICIAR", "SALIR",
};

static const char* NAMES_TXT =
	"NOMBRES QUE DEBEN TENER LAS ROMS\n================================\n\n"
	"Pon tus ROMs de Sonic Advance (copias de tus propios cartuchos) en ESTA carpeta\n(main/roms) y renombralas EXACTAMENTE asi:\n\n"
	"    Sonic Advance     ->  sonicadvance.gba\n    Sonic Advance 2   ->  sonicadvance2.gba\n    Sonic Advance 3   ->  sonicadvance3.gba\n\n"
	"Los juegos cuya ROM no este en esta carpeta apareceran oscurecidos en el launcher.\n"
	"Si anades una ROM con el launcher abierto, se detecta sola en un segundo.\n";
static const char* BIOS_TXT =
	"BIOS DE GBA (OPCIONAL)\n======================\nLos juegos funcionan sin BIOS (mGBA incluye una compatible).\n"
	"Si quieres usar la original, copia aqui tu archivo y llamalo exactamente:  gba_bios.bin\n";

int* cfg_field(Config* c, int field) {
	switch (field) {
	case 0: return &c->fullscreen;
	case 1: return &c->volume;
	case 2: return &c->smooth;
	case 3: return &c->integer_scale;
	default: return &c->show_fps;
	}
}

/* ---------------------------------------------------------------- utilidades */
static void write_if_missing(const char* path, const char* text) {
	if (sys_file_exists(path)) return;
	FILE* f = fopen(path, "w");
	if (f) { fputs(text, f); fclose(f); }
}

static void toast_s(App* a, double secs, const char* fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(a->toast, sizeof(a->toast), fmt, ap);
	va_end(ap);
	a->toast_until = a->now + secs;
}
void app_toast(App* a, const char* fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(a->toast, sizeof(a->toast), fmt, ap);
	va_end(ap);
	a->toast_until = a->now + 3.5;
}
void app_modal(App* a, const char* title, const char* text) {
	a->modal = 1;
	a->modal_kind = 0;
	snprintf(a->modal_title, sizeof(a->modal_title), "%s", title);
	snprintf(a->modal_text, sizeof(a->modal_text), "%s", text);
}

void key_label(const App* a, int code, char* out, size_t n) {
	if (code < 0) { snprintf(out, n, "— sin asignar —"); return; }
	switch (code) {
	case K_RETURN: snprintf(out, n, "Enter"); return;
	case K_SPACE: snprintf(out, n, "Espacio"); return;
	case K_ESCAPE: snprintf(out, n, "Esc"); return;
	case K_BACKSPACE: snprintf(out, n, "Retroceso"); return;
	case K_TAB: snprintf(out, n, "Tab"); return;
	case K_DELETE: snprintf(out, n, "Supr"); return;
	case K_LEFT: snprintf(out, n, "Flecha izq."); return;
	case K_RIGHT: snprintf(out, n, "Flecha der."); return;
	case K_UP: snprintf(out, n, "Flecha arriba"); return;
	case K_DOWN: snprintf(out, n, "Flecha abajo"); return;
	case K_F1: snprintf(out, n, "F1"); return;
	case K_F5: snprintf(out, n, "F5"); return;
	case K_F8: snprintf(out, n, "F8"); return;
	case K_F11: snprintf(out, n, "F11"); return;
	case K_RCTRL: snprintf(out, n, "Ctrl derecho"); return;
	case K_RSHIFT: snprintf(out, n, "Shift derecho"); return;
	case K_RALT: snprintf(out, n, "AltGr"); return;
	default: break;
	}
	if (code > 32 && code < 127) { snprintf(out, n, "%c", toupper(code)); return; }
	if (a->host.key_name) {
		const char* s = a->host.key_name(a->host.user, code);
		if (s && s[0]) { snprintf(out, n, "%s", s); return; }
	}
	snprintf(out, n, "Tecla %d", code);
}

void pad_label(int code, char* out, size_t n) {
	static const char* names[] = {"Cruz (X)", "Círculo (O)", "Cuadrado", "Triángulo", "Share", "Botón PS", "Options",
	                              "L3", "R3", "L1", "R1", "Cruceta arriba", "Cruceta abajo", "Cruceta izq.", "Cruceta der."};
	if (code < 0) snprintf(out, n, "— sin asignar —");
	else if (code <= 14) snprintf(out, n, "%s", names[code]);
	else if (code == 20) snprintf(out, n, "Panel táctil");
	else if (code == 100) snprintf(out, n, "L2");
	else if (code == 101) snprintf(out, n, "R2");
	else snprintf(out, n, "Botón %d", code);
}

/* conjuntos pequeños de entradas pulsadas */
static int set_has(const int* s, int n, int v) { for (int i = 0; i < n; i++) if (s[i] == v) return 1; return 0; }
static void set_add(int* s, int* n, int v) { if (!set_has(s, *n, v) && *n < MAXSET) s[(*n)++] = v; }
static void set_del(int* s, int* n, int v) { for (int i = 0; i < *n; i++) if (s[i] == v) { s[i] = s[--(*n)]; return; } }
static void clear_inputs(App* a) {
	a->nkeys = a->npad = a->nstick = 0;
	a->p2_npad = a->p2_nstick = 0;
	a->p2_trig[0] = a->p2_trig[1] = 0;
	a->p2_stick[0] = a->p2_stick[1] = 0.0f;
	memset(a->touch_down, 0, sizeof(a->touch_down));
	for (int i = 0; i < 4; i++) a->held_dir[i] = 0;
	a->stick_dir = -1;
}

/* ------------------------------------------------------------ configuración */
void app_load_config(App* a) { config_defaults(&a->cfg); config_load(&a->cfg, a->cfg_path); }
void app_save_config(App* a) { config_save(&a->cfg, a->cfg_path); }

/* ----------------------------------------------------------------------- ROMs */
typedef struct { App* a; } ScanCtx;
static void scan_cb(const char* name, void* ctx) {
	App* a = ((ScanCtx*) ctx)->a;
	for (int i = 0; i < NUM_GAMES; i++)
		if (sys_strcasecmp(name, GAMES[i].rom) == 0) {
			snprintf(a->rom_path[i], PATH_MAX_APP, "%s/%s", a->roms_dir, name);
			a->rom_found[i] = 1;
		}
}
void app_scan_roms(App* a) {
	ScanCtx ctx = {a};
	for (int i = 0; i < NUM_GAMES; i++) { a->rom_found[i] = 0; a->rom_path[i][0] = 0; }
	sys_list_dir(a->roms_dir, scan_cb, &ctx);
	a->last_scan = a->now;
}

/* -------------------------------------------------------------- ranuras de guardado */
void slot_path(const App* a, int n, char* out, size_t sz) {
	char stem[128];
	sys_stem(a->rom_path[a->game_idx][0] ? a->rom_path[a->game_idx] : GAMES[a->game_idx].rom, stem, sizeof(stem));
	snprintf(out, sz, "%s/%s.ss%d", a->saves_dir, stem, n);
}
int slot_exists(const App* a, int n) {
	char p[PATH_MAX_APP + 16];
	slot_path(a, n, p, sizeof(p));
	return sys_file_exists(p);
}
void app_load_thumbs(App* a) {
	for (int n = 1; n <= SLOT_COUNT; n++) {
		cv_free(a->slot_thumbs[n]);
		a->slot_thumbs[n] = NULL;
		char p[PATH_MAX_APP + 16];
		slot_path(a, n, p, sizeof(p));
		if (!sys_file_exists(p)) continue;
		strcat(p, ".png");
		Canvas* img = cv_load_png(p);
		if (img) {
			a->slot_thumbs[n] = cv_resize(img, 360, 240);
			cv_free(img);
			if (a->slot_thumbs[n]) cv_round_corners(a->slot_thumbs[n], 12);
		}
	}
}

static int save_slot(App* a, int n) {
	char p[PATH_MAX_APP + 16];
	sys_mkdir_p(a->saves_dir);
	slot_path(a, n, p, sizeof(p));
	if (!emu_save_state(a->emu, p)) return 0;
	uint32_t thumb[EMU_W * EMU_H];
	for (int i = 0; i < EMU_W * EMU_H; i++) {
		uint32_t v = a->frame[i];
		thumb[i] = 0xFF000000u | ((v & 255) << 16) | (v & 0xFF00) | ((v >> 16) & 255);
	}
	strcat(p, ".png");
	png_save_file(p, thumb, EMU_W, EMU_H, EMU_W, 0);
	return 1;
}

static void delete_slot(App* a, int n) {
	if (!slot_exists(a, n)) return;
	if (a->delete_slot == n && a->now - a->delete_t < 3.0) {
		char p[PATH_MAX_APP + 16];
		slot_path(a, n, p, sizeof(p));
		sys_remove(p);
		strcat(p, ".png");
		sys_remove(p);
		a->delete_slot = -1;
		app_load_thumbs(a);
		app_toast(a, "Ranura %d borrada", n);
	} else {
		a->delete_slot = n;
		a->delete_t = a->now;
		app_toast(a, "Pulsa BORRAR otra vez para eliminar la ranura %d", n);
	}
}

static void resume_game(App* a);
static void do_slot(App* a, int n) {
	char p[PATH_MAX_APP + 16];
	if (a->slot_mode == 0) {
		if (save_slot(a, n)) { a->last_slot = n; app_load_thumbs(a); app_toast(a, "Partida guardada en la ranura %d", n); }
		else app_toast(a, "No se pudo guardar en la ranura %d", n);
	} else {
		if (!slot_exists(a, n)) { app_toast(a, "La ranura %d está vacía", n); return; }
		slot_path(a, n, p, sizeof(p));
		if (emu_load_state(a->emu, p)) { a->last_slot = n; app_toast(a, "Partida cargada (ranura %d)", n); resume_game(a); }
		else app_toast(a, "No se pudo cargar la ranura %d", n);
	}
}

static void quick_save(App* a) {
	int linked = a->emu2 != NULL;
	if (linked) emu_set_paused(a->emu, 1);
	int ok = save_slot(a, a->last_slot);
	if (linked) emu_set_paused(a->emu, 0);
	if (ok) toast_s(a, 2.0, "Guardado rápido (ranura %d)", a->last_slot);
	else toast_s(a, 2.0, "No se pudo guardar");
}
static void quick_load(App* a) {
	char p[PATH_MAX_APP + 16];
	slot_path(a, a->last_slot, p, sizeof(p));
	int linked = a->emu2 != NULL;
	if (linked) emu_set_paused(a->emu, 1);
	int ok = slot_exists(a, a->last_slot) && emu_load_state(a->emu, p);
	if (linked) emu_set_paused(a->emu, 0);
	if (ok) {
		toast_s(a, 2.0, "Carga rápida (ranura %d)", a->last_slot);
		a->audio_frames = 0; a->audio_reset = 1;
	} else toast_s(a, 2.0, "La ranura %d está vacía", a->last_slot);
}

/* ------------------------------------------------------------------- juego */
static void set_cursor(App* a) {
	if (a->host.set_cursor_visible) a->host.set_cursor_visible(a->host.user, a->state != ST_GAME && !a->input_pad);
}

static int s_logged_first_audio_chunk;

static void start_game_now(App* a, int i, int multiplayer) {
	char stem[128], save[PATH_MAX_APP + 16], save2[PATH_MAX_APP + 24];
	if (!a->rom_found[i]) { app_toast(a, "Falta la ROM: %s", GAMES[i].rom); return; }
	sys_mkdir_p(a->saves_dir);
	sys_stem(a->rom_path[i], stem, sizeof(stem));
	snprintf(save, sizeof(save), "%s/%s.sav", a->saves_dir, stem);
	a->emu2 = NULL;
	if (multiplayer) {
		snprintf(save2, sizeof(save2), "%s/%s.p2.sav", a->saves_dir, stem);
		a->emu = emu_open_linked_pair(a->rom_path[i], save, save2,
		                             sys_file_exists(a->bios_file) ? a->bios_file : NULL,
		                             a->host.audio_rate > 0 ? (unsigned) a->host.audio_rate : 48000u,
		                             &a->emu2);
	} else {
		a->emu = emu_open(a->rom_path[i], save, sys_file_exists(a->bios_file) ? a->bios_file : NULL,
		                  a->host.audio_rate > 0 ? (unsigned) a->host.audio_rate : 48000u);
	}
	if (!a->emu) {
		a->emu2 = NULL;
		debug_log_error("No se pudo iniciar %s en modo %s", GAMES[i].title, multiplayer ? "multijugador" : "un jugador");
		app_modal(a, "No se pudo iniciar el juego", multiplayer
		          ? "mGBA no pudo crear la conexión por cable link. Comprueba que la ROM de GBA sea válida."
		          : "mGBA no pudo abrir la ROM. Comprueba que es una ROM de GBA válida (.gba sin comprimir).");
		return;
	}
	a->game_idx = i;
	a->audio_ratio = 1.0;
	a->audio_fill_avg = 0.0;
	s_logged_first_audio_chunk = 0;
	debug_log_info("Juego iniciado: %s; modo=%s; volumen=%d%%; frecuencia de salida=%d Hz",
	               GAMES[i].title, a->emu2 ? "dos jugadores" : "un jugador",
	               a->cfg.volume, a->host.audio_rate);
	a->have_frame = 0;
	a->audio_frames = 0;
	a->last_slot = 1;
	clear_inputs(a);
	a->state = ST_GAME;
	a->audio_reset = 1;
	set_cursor(a);
	char title[160];
	snprintf(title, sizeof(title), "%s%s - Sonic Advance Trilogy", GAMES[i].title,
	         a->emu2 ? " [2 jugadores]" : "");
	if (a->host.set_title) a->host.set_title(a->host.user, title);
	if (a->emu2) app_toast(a, "Pantalla dividida: J1 izquierda · J2 derecha");
}

static void start_game(App* a, int i) {
	if (!a->rom_found[i]) { app_toast(a, "Falta la ROM: %s", GAMES[i].rom); return; }
#if !defined(__EMSCRIPTEN__)
	a->modal = 1;
	a->modal_kind = 1;
	a->modal_choice = 0;
	a->prompt_game = i;
	snprintf(a->modal_title, sizeof(a->modal_title), "¿CÓMO DESEA JUGAR?");
	snprintf(a->modal_text, sizeof(a->modal_text),
	         a->pad_count >= 2
	           ? "Hay dos mandos detectados. Puedes usarlos o elegir jugar con teclado."
	           : "El modo 2J también funciona sin mandos: J1 usa WASD y J2 las flechas.");
	return;
#else
	start_game_now(a, i, 0);
#endif
}

static void resolve_game_mode(App* a, int two_players) {
	int game = a->prompt_game;
	a->modal = 0;
	a->modal_kind = 0;
	if (game < 0 || game >= NUM_GAMES) return;
	/* El modo 2J enlaza dos núcleos mGBA incluso sin mandos; las teclas de ambos
	 * jugadores se transforman en máscaras de entrada independientes. */
	start_game_now(a, game, two_players != 0);
}

static void pause_game(App* a) {
	if (a->state != ST_GAME) return;
	a->state = ST_PAUSE;
	a->pause_sel = 0;
	for (int i = 0; i < PAUSE_COUNT; i++) a->pause_anim[i] = 0;
	emu_set_keys(a->emu, 0);
	if (a->emu2) emu_set_keys(a->emu2, 0);
	emu_set_paused(a->emu, 1);
	clear_inputs(a);
	a->audio_frames = 0;
	a->audio_reset = 1;
	set_cursor(a);
}

static void resume_game(App* a) {
	a->state = ST_GAME;
	emu_set_paused(a->emu, 0);
	clear_inputs(a);
	emu_clear_audio(a->emu);
	if (a->emu2) emu_clear_audio(a->emu2);
	a->audio_frames = 0;
	a->audio_reset = 1;
	set_cursor(a);
}

static void restart_game(App* a) {
	int game = a->game_idx;
	int multiplayer = a->emu2 != NULL;
	if (a->emu) emu_close(a->emu);
	a->emu = NULL;
	a->emu2 = NULL;
	a->state = ST_MAIN;
	start_game_now(a, game, multiplayer);
	if (!a->emu) {
		a->state = ST_MAIN;
		set_cursor(a);
	}
}

static void exit_game(App* a) {
	if (a->emu) emu_close(a->emu);
	a->emu = NULL;
	a->emu2 = NULL;
	a->have_frame = 0;
	a->state = ST_MAIN;
	for (int n = 1; n <= SLOT_COUNT; n++) { cv_free(a->slot_thumbs[n]); a->slot_thumbs[n] = NULL; }
	if (a->host.set_title) a->host.set_title(a->host.user, "Sonic Advance Trilogy Launcher");
	set_cursor(a);
	app_scan_roms(a);
}

static const int GBA_BIT[ACT_COUNT] = {0, 1, 8, 9, 3, 2, 6, 7, 5, 4, -1, -1, -1, -1};

static int pressed_id(const App* a, int id) {
	if (id < 0) return 0;
	if (set_has(a->pad_down, a->npad, id) || set_has(a->stick_down, a->nstick, id)) return 1;
	return (id == PAD_L2 && a->trig[0]) || (id == PAD_R2 && a->trig[1]);
}
/* En 2J sin mandos, mapa clásico de teclado compartido:
 * J1: WASD, X/Z, Q/E, 2=Start, 1=Select.
 * J2: flechas, Ctrl derecho=A, Alt derecho/AltGr=B, Enter=Start,
 * Shift derecho=Select. Cada tabla está separada para permitir que teclado y
 * mando funcionen simultáneamente. */
static const int dual_keyboard_keys[2][ACT_COUNT] = {
	{'x', 'z', 'q', 'e', '2', '1', 'w', 's', 'a', 'd', K_ESCAPE, K_TAB, K_F5, K_F8},
	{K_RCTRL, K_RALT, -1, -1, K_RETURN, K_RSHIFT, K_UP, K_DOWN, K_LEFT, K_RIGHT, -1, -1, -1, -1}
};
static uint32_t dual_keyboard_mask(const App* a, int player) {
	uint32_t m = 0;
	if (player < 0 || player > 1) return 0;
	for (int i = 0; i < ACT_COUNT; i++) {
		int k = dual_keyboard_keys[player][i];
		if (GBA_BIT[i] >= 0 && k >= 0 && set_has(a->keys_down, a->nkeys, k)) m |= 1u << GBA_BIT[i];
	}
	return m;
}
static uint32_t keymask(const App* a) {
	uint32_t m = (a->emu2 && a->pad_count == 0) ? dual_keyboard_mask(a, 0) : 0;
	for (int i = 0; i < ACT_COUNT; i++) {
		if (GBA_BIT[i] < 0) continue;
		if (!a->emu2) {
			int k = a->cfg.keys[i];
			if (k >= 0 && set_has(a->keys_down, a->nkeys, k)) m |= 1u << GBA_BIT[i];
		}
		if (pressed_id(a, a->cfg.pad[0][i]) || a->touch_down[i]) m |= 1u << GBA_BIT[i];
	}
	return m;
}
static int pressed_id2(const App* a, int id) {
	if (id < 0) return 0;
	if (set_has(a->p2_pad_down, a->p2_npad, id) || set_has(a->p2_stick_down, a->p2_nstick, id)) return 1;
	return (id == PAD_L2 && a->p2_trig[0]) || (id == PAD_R2 && a->p2_trig[1]);
}
static uint32_t keymask2(const App* a) {
	uint32_t m = (a->emu2 && a->pad_count == 0) ? dual_keyboard_mask(a, 1) : 0;
	for (int i = 0; i < ACT_COUNT; i++) {
		if (GBA_BIT[i] < 0) continue;
		if (pressed_id2(a, a->cfg.pad[1][i])) m |= 1u << GBA_BIT[i];
	}
	return m;
}
static int turbo_active(const App* a) {
	int k = a->cfg.keys[ACT_TURBO];
	return (k >= 0 && set_has(a->keys_down, a->nkeys, k)) ||
	       pressed_id(a, a->cfg.pad[0][ACT_TURBO]) || a->touch_down[ACT_TURBO];
}

/* ------------------------------------------------------------------- ajustes */
static void toggle_fullscreen(App* a) {
	a->cfg.fullscreen = !a->cfg.fullscreen;
	app_save_config(a);
	if (a->host.set_fullscreen) a->host.set_fullscreen(a->host.user, a->cfg.fullscreen);
}
static void adjust_volume(App* a, int d) {
	int v = a->cfg.volume + d;
	a->cfg.volume = v < 0 ? 0 : (v > 100 ? 100 : v);
	app_save_config(a);
}
static void assign(App* a, int kind, int act, int code) {
	if (act < 0 || act >= ACT_COUNT || kind < 0 || kind > 2) return;
	int* tbl = kind == 0 ? a->cfg.keys : a->cfg.pad[kind - 1];
	if (code >= 0)
		for (int o = 0; o < ACT_COUNT; o++)
			if (o != act && tbl[o] == code) tbl[o] = tbl[act];   /* intercambio solo dentro de ese dispositivo */
	tbl[act] = code;
	app_save_config(a);
}

static void open_settings(App* a) { a->state = ST_SETTINGS; a->settings_page = 0; a->set_sel = 0; a->held_dir[0] = a->held_dir[1] = a->held_dir[2] = a->held_dir[3] = 0; }
static void open_controls(App* a, int from_pause) {
	a->controls_from_pause = from_pause;
	a->state = ST_SETTINGS; a->settings_page = 1; a->ctrl_row = a->ctrl_col = 0; a->capture_kind = -1;
	clear_inputs(a);
}
static void leave_controls(App* a) {
	a->capture_kind = -1;
	if (a->controls_from_pause) a->state = ST_PAUSE; else a->settings_page = 0;
}
static void open_folder_roms(App* a) {
	if (a->host.open_folder) a->host.open_folder(a->host.user, a->roms_dir);
	else app_modal(a, "Carpeta de ROMs", a->roms_dir);
}

static void settings_action(App* a, int act) {
	if (act == A_UP) a->set_sel = (a->set_sel + SET_COUNT - 1) % SET_COUNT;
	else if (act == A_DOWN) a->set_sel = (a->set_sel + 1) % SET_COUNT;
	else if (act == A_BACK || act == A_MENU) a->state = ST_MAIN;
	else if (act == A_ACCEPT || act == A_LEFT || act == A_RIGHT) {
		const SettingItem* it = &SETTINGS[a->set_sel];
		if (it->kind == 0) {
			int* f = cfg_field(&a->cfg, it->field);
			*f = !*f;
			app_save_config(a);
			if (it->field == 0 && a->host.set_fullscreen) a->host.set_fullscreen(a->host.user, a->cfg.fullscreen);
		} else if (it->kind == 1) {
			if (act == A_LEFT) adjust_volume(a, -10);
			else if (act == A_RIGHT) adjust_volume(a, 10);
		} else if (act == A_ACCEPT) {
			if (it->kind == 2) open_controls(a, 0);
			else if (it->kind == 3) open_folder_roms(a);
			else if (it->kind == 4) a->state = ST_MAIN;
		}
	}
}

static void controls_action(App* a, int act) {
	int n = ACT_COUNT + 2;
	if (act == A_UP) a->ctrl_row = (a->ctrl_row + n - 1) % n;
	else if (act == A_DOWN) a->ctrl_row = (a->ctrl_row + 1) % n;
	else if (act == A_LEFT || act == A_RIGHT) {
		if (a->ctrl_row < ACT_COUNT) {
			a->ctrl_col += act == A_LEFT ? -1 : 1;
			if (a->ctrl_col < 0) a->ctrl_col = 2;
			if (a->ctrl_col > 2) a->ctrl_col = 0;
		}
	}
	else if (act == A_BACK || act == A_MENU) leave_controls(a);
	else if (act == A_EXTRA) { if (a->ctrl_row < ACT_COUNT) assign(a, a->ctrl_col, a->ctrl_row, -1); }
	else if (act == A_ACCEPT) {
		if (a->ctrl_row < ACT_COUNT) { a->capture_kind = a->ctrl_col; a->capture_act = a->ctrl_row; }
		else if (a->ctrl_row == ACT_COUNT) {
			Config d;
			config_defaults(&d);
			memcpy(a->cfg.keys, d.keys, sizeof(d.keys));
			memcpy(a->cfg.pad, d.pad, sizeof(d.pad));
			app_save_config(a);
			app_toast(a, "Controles restablecidos");
		} else leave_controls(a);
	}
}

static void open_slots(App* a, int mode) {
	a->slot_mode = mode;
	a->state = ST_SLOTS;
	a->delete_slot = -1;
	app_load_thumbs(a);
	int first = a->last_slot - 1;
	if (mode == 1) { first = 0; for (int n = 0; n < SLOT_COUNT; n++) if (slot_exists(a, n + 1)) { first = n; break; } }
	a->slot_sel = first < 0 ? 0 : (first >= SLOT_COUNT ? SLOT_COUNT - 1 : first);
}

static void slots_action(App* a, int act) {
	if (act == A_LEFT) { if (a->slot_sel % 4 > 0) a->slot_sel--; }
	else if (act == A_RIGHT) { if (a->slot_sel % 4 < 3) a->slot_sel++; }
	else if (act == A_UP) { if (a->slot_sel >= 4) a->slot_sel -= 4; }
	else if (act == A_DOWN) { if (a->slot_sel < 4) a->slot_sel += 4; }
	else if (act == A_BACK || act == A_MENU) a->state = ST_PAUSE;
	else if (act == A_ACCEPT) do_slot(a, a->slot_sel + 1);
	else if (act == A_EXTRA) delete_slot(a, a->slot_sel + 1);
}

static void pause_action(App* a, int act) {
	if (act == A_UP) a->pause_sel = (a->pause_sel + PAUSE_COUNT - 1) % PAUSE_COUNT;
	else if (act == A_DOWN) a->pause_sel = (a->pause_sel + 1) % PAUSE_COUNT;
	else if (act == A_BACK || act == A_MENU) resume_game(a);
	else {
		switch (a->pause_sel) {
		case 3:
			if (act == A_LEFT) adjust_volume(a, -10);
			else if (act == A_RIGHT) adjust_volume(a, 10);
			else if (act == A_ACCEPT) adjust_volume(a, a->cfg.volume < 100 ? 10 : -100);
			break;
		case 4: if (act == A_LEFT || act == A_RIGHT || act == A_ACCEPT) toggle_fullscreen(a); break;
		default:
			if (act != A_ACCEPT) break;
			if (a->pause_sel == 0) resume_game(a);
			else if (a->pause_sel == 1) open_slots(a, 0);
			else if (a->pause_sel == 2) open_slots(a, 1);
			else if (a->pause_sel == 5) open_controls(a, 1);
			else if (a->pause_sel == 6) {
				if (a->emu2) restart_game(a);
				else { emu_reset(a->emu); resume_game(a); }
			}
			else if (a->pause_sel == 7) exit_game(a);
		}
	}
}

static void main_action(App* a, int act) {
	if (act == A_PREV) { if (a->sel > 0) a->sel--; return; }
	if (act == A_NEXT) { if (a->sel < NUM_GAMES - 1) a->sel++; return; }
	if (act == A_MENU) { open_settings(a); return; }
	if (a->zone == 0) {
		if (act == A_LEFT) { if (a->sel > 0) a->sel--; }
		else if (act == A_RIGHT) { if (a->sel < NUM_GAMES - 1) a->sel++; }
		else if (act == A_DOWN) { a->zone = 1; a->btn = 0; }
		else if (act == A_ACCEPT) a->pending_launch = a->sel;
	} else {
		if (act == A_LEFT) { if (a->btn > 0) a->btn--; }
		else if (act == A_RIGHT) { if (a->btn < 3) a->btn++; }
		else if (act == A_UP || act == A_BACK) a->zone = 0;
		else if (act == A_ACCEPT) {
			if (a->btn == 0) a->pending_launch = a->sel;
			else if (a->btn == 1) open_folder_roms(a);
			else if (a->btn == 2) open_settings(a);
			else a->quit = 1;
		}
	}
}

static void request_launch_check(App* a) {
	if (a->pending_launch >= 0 && !a->rom_found[a->pending_launch]) {
		app_toast(a, "Falta la ROM: pon \"%s\" en main/roms", GAMES[a->pending_launch].rom);
		a->pending_launch = -1;
	}
}

static void dispatch(App* a, int act) {
	if (a->modal) {
		if (a->modal_kind == 1) {
			if (act == A_LEFT || act == A_UP) a->modal_choice = 0;
			else if (act == A_RIGHT || act == A_DOWN) a->modal_choice = 1;
			else if (act == A_ACCEPT) resolve_game_mode(a, a->modal_choice == 1);
			else if (act == A_BACK) resolve_game_mode(a, 0);
		} else if (act == A_ACCEPT || act == A_BACK) a->modal = 0;
		return;
	}
	switch (a->state) {
	case ST_SETTINGS: if (a->settings_page) controls_action(a, act); else settings_action(a, act); break;
	case ST_PAUSE: pause_action(a, act); break;
	case ST_SLOTS: slots_action(a, act); break;
	case ST_MAIN: main_action(a, act); request_launch_check(a); break;
	default: break;
	}
}

static void press_dir(App* a, int d) { dispatch(a, d); a->held_dir[d] = 1; a->held_t[d] = a->now + 0.42; }
static void release_dir(App* a, int d) { a->held_dir[d] = 0; }

static void set_pad_mode(App* a, int pad) {
	if (a->input_pad != pad) { a->input_pad = pad; set_cursor(a); }
}

static void update_stick(App* a) {
	float x = a->stick[0], y = a->stick[1];
	int d = -1;
	if (fabsf(x) > 0.6f || fabsf(y) > 0.6f) d = fabsf(x) >= fabsf(y) ? (x > 0 ? A_RIGHT : A_LEFT) : (y > 0 ? A_DOWN : A_UP);
	if (d != a->stick_dir) {
		if (a->stick_dir >= 0) release_dir(a, a->stick_dir);
		a->stick_dir = d;
		if (d >= 0) { set_pad_mode(a, 1); press_dir(a, d); }
	}
}
static void update_game_stick(App* a) {
	a->nstick = 0;
	if (a->stick[0] < -0.5f) set_add(a->stick_down, &a->nstick, PAD_LEFT);
	else if (a->stick[0] > 0.5f) set_add(a->stick_down, &a->nstick, PAD_RIGHT);
	if (a->stick[1] < -0.5f) set_add(a->stick_down, &a->nstick, PAD_UP);
	else if (a->stick[1] > 0.5f) set_add(a->stick_down, &a->nstick, PAD_DOWN);
}

static int key_action(int k) {
	switch (k) {
	case K_LEFT: case 'a': return A_LEFT;
	case K_RIGHT: case 'd': return A_RIGHT;
	case K_UP: case 'w': return A_UP;
	case K_DOWN: case 's': return A_DOWN;
	case K_RETURN: case K_SPACE: return A_ACCEPT;
	case K_ESCAPE: case K_BACKSPACE: return A_BACK;
	case K_TAB: case K_F1: return A_MENU;
	case K_DELETE: return A_EXTRA;
	case 'q': return A_PREV;
	case 'e': return A_NEXT;
	default: return A_NONE;
	}
}

static void on_key_down(App* a, int k, int alt) {
	set_pad_mode(a, 0);
	if (a->capture_kind >= 0) {
		if (k == K_ESCAPE) a->capture_kind = -1;
		else if (k == K_DELETE) { assign(a, a->capture_kind, a->capture_act, -1); a->capture_kind = -1; }
		else if (a->capture_kind == 0) { assign(a, 0, a->capture_act, k); a->capture_kind = -1; }
		return;
	}
	/* En 2J se reserva AltGr + Enter porque AltGr es el botón B de J2
	 * y Enter es Start; fuera de 2J se conserva Alt+Enter para pantalla completa. */
	if (k == K_F11 || (k == K_RETURN && alt && !a->emu2)) { toggle_fullscreen(a); return; }
	if (a->state == ST_GAME) {
		if (a->emu2) {
			/* En 2J la tecla de pausa y los guardados rápidos siguen siendo globales;
			 * las teclas de acción se leen luego con mapas separados por jugador. */
			if (k == K_ESCAPE) pause_game(a);
			else if (k == K_F5) quick_save(a);
			else if (k == K_F8) quick_load(a);
			else set_add(a->keys_down, &a->nkeys, k);
		} else {
			if (k == a->cfg.keys[ACT_PAUSE]) pause_game(a);
			else if (k == a->cfg.keys[ACT_QSAVE]) quick_save(a);
			else if (k == a->cfg.keys[ACT_QLOAD]) quick_load(a);
			else set_add(a->keys_down, &a->nkeys, k);
		}
		return;
	}
	if (a->state == ST_PAUSE && k == a->cfg.keys[ACT_PAUSE] && k != K_ESCAPE) { resume_game(a); return; }
	int act = key_action(k);
	if (act >= A_UP && act <= A_RIGHT) press_dir(a, act);
	else if (act != A_NONE) dispatch(a, act);
}

static void on_pad_button(App* a, int id, int down, int player) {
	/* Capture para cada columna física (1 = mando J1, 2 = mando J2). */
	if (a->capture_kind >= 0) {
		if (a->capture_kind == 0) return; /* se está esperando una tecla */
		if (a->capture_kind == player + 1) {
			set_pad_mode(a, 1);
			if (down) { assign(a, a->capture_kind, a->capture_act, id); a->capture_kind = -1; }
		}
		return;
	}
	if (player == 1) {
		/* J2 solo controla su núcleo durante una sesión enlazada. */
		if (a->state != ST_GAME || !a->emu2) return;
		if (down) set_add(a->p2_pad_down, &a->p2_npad, id);
		else set_del(a->p2_pad_down, &a->p2_npad, id);
		return;
	}
	set_pad_mode(a, 1);
	if (a->state == ST_GAME) {
		if (down) {
			if (id == a->cfg.pad[0][ACT_PAUSE]) pause_game(a);
			else if (id == a->cfg.pad[0][ACT_QSAVE]) quick_save(a);
			else if (id == a->cfg.pad[0][ACT_QLOAD]) quick_load(a);
			else set_add(a->pad_down, &a->npad, id);
		} else set_del(a->pad_down, &a->npad, id);
		return;
	}
	if (id >= PAD_UP && id <= PAD_RIGHT) {
		static const int map[4] = {A_UP, A_DOWN, A_LEFT, A_RIGHT};
		if (down) press_dir(a, map[id - PAD_UP]); else release_dir(a, map[id - PAD_UP]);
		return;
	}
	if (!down) return;
	if (a->state == ST_PAUSE && id == a->cfg.pad[0][ACT_PAUSE]) { resume_game(a); return; }
	switch (id) {
	case PAD_CROSS: dispatch(a, A_ACCEPT); break;
	case PAD_CIRCLE: dispatch(a, A_BACK); break;
	case PAD_SQUARE: dispatch(a, A_EXTRA); break;
	case PAD_OPTIONS: dispatch(a, A_MENU); break;
	case PAD_L1: dispatch(a, A_PREV); break;
	case PAD_R1: dispatch(a, A_NEXT); break;
	default: break;
	}
}

static void on_pad_axis(App* a, int axis, int value, int player) {
	float v = value / 32768.0f;
	if (axis == 4 || axis == 5) {
		int down = v > 0.5f;
		if (down && a->capture_kind == player + 1) {
			assign(a, player + 1, a->capture_act, PAD_L2 + axis - 4);
			a->capture_kind = -1;
			return;
		}
	}
	if (player == 1) {
		if (a->state != ST_GAME || !a->emu2) return;
		if (axis == 0 || axis == 1) {
			a->p2_stick[axis] = v;
			a->p2_nstick = 0;
			if (a->p2_stick[0] < -0.5f) set_add(a->p2_stick_down, &a->p2_nstick, PAD_LEFT);
			else if (a->p2_stick[0] > 0.5f) set_add(a->p2_stick_down, &a->p2_nstick, PAD_RIGHT);
			if (a->p2_stick[1] < -0.5f) set_add(a->p2_stick_down, &a->p2_nstick, PAD_UP);
			else if (a->p2_stick[1] > 0.5f) set_add(a->p2_stick_down, &a->p2_nstick, PAD_DOWN);
		} else if (axis == 4 || axis == 5) a->p2_trig[axis - 4] = v > 0.5f;
		return;
	}
	if (axis == 0 || axis == 1) {
		a->stick[axis] = v;
		if (a->state == ST_GAME) update_game_stick(a); else update_stick(a);
	} else if (axis == 4 || axis == 5) {
		int i = axis - 4, down = v > 0.5f;
		if (down != a->trig[i]) {
			a->trig[i] = down;
			if (a->state == ST_GAME && down && a->cfg.pad[0][ACT_PAUSE] == PAD_L2 + i) pause_game(a);
		}
	}
}

/* ratón (coordenadas de la interfaz) */
static void mouse_move(App* a, int x, int y) {
	if (a->modal || a->state == ST_GAME) return;
	if (a->state == ST_SETTINGS) {
		if (a->settings_page) {
			for (int k = 0; k < ACT_COUNT + 2; k++) if (rect_has(ui_ctrl_row_rect(k), x, y)) {
				a->ctrl_row = k;
				if (k < ACT_COUNT) {
					for (int col = 0; col < 3; col++)
						if (rect_has(ui_ctrl_cell_rect(k, col), x, y)) a->ctrl_col = col;
				}
			}
		} else for (int k = 0; k < SET_COUNT; k++) if (rect_has(ui_settings_row(k), x, y)) a->set_sel = k;
	} else if (a->state == ST_PAUSE) {
		for (int i = 0; i < PAUSE_COUNT; i++) if (rect_has(ui_pause_rect(i), x, y)) a->pause_sel = i;
	} else if (a->state == ST_SLOTS) {
		for (int n = 0; n < SLOT_COUNT; n++) if (rect_has(ui_slot_rect(n), x, y)) a->slot_sel = n;
	} else {
		for (int i = 0; i < NUM_GAMES; i++) if (rect_has(ui_card_rect(a, i), x, y)) { a->sel = i; a->zone = 0; return; }
		for (int j = 0; j < 4; j++) if (rect_has(ui_btn_rect(j), x, y)) { a->zone = 1; a->btn = j; return; }
	}
}
static void mouse_click(App* a, int x, int y) {
	if (a->state == ST_GAME) return;
	if (a->modal) {
		if (a->modal_kind == 1) {
			if (rect_has(ui_modal_yes(), x, y)) resolve_game_mode(a, 0);
			else if (rect_has(ui_modal_no(), x, y)) resolve_game_mode(a, 1);
		} else if (rect_has(ui_modal_ok(), x, y)) a->modal = 0;
		return;
	}
	if (a->capture_kind >= 0) { a->capture_kind = -1; return; }
	if (a->state == ST_MAIN) {
		for (int i = 0; i < NUM_GAMES; i++) if (rect_has(ui_card_rect(a, i), x, y)) {
			int was = (a->sel == i && a->zone == 0);
			a->sel = i; a->zone = 0;
			if (was) { a->pending_launch = i; request_launch_check(a); }
			return;
		}
		for (int j = 0; j < 4; j++) if (rect_has(ui_btn_rect(j), x, y)) { a->zone = 1; a->btn = j; dispatch(a, A_ACCEPT); return; }
		return;
	}
	int hit = 0;
	mouse_move(a, x, y);
	if (a->state == ST_SETTINGS) {
		if (a->settings_page) { for (int k = 0; k < ACT_COUNT + 2; k++) if (rect_has(ui_ctrl_row_rect(k), x, y)) hit = 1; }
		else for (int k = 0; k < SET_COUNT; k++) if (rect_has(ui_settings_row(k), x, y)) hit = 1;
	} else if (a->state == ST_PAUSE) { for (int i = 0; i < PAUSE_COUNT; i++) if (rect_has(ui_pause_rect(i), x, y)) hit = 1; }
	else if (a->state == ST_SLOTS) { for (int n = 0; n < SLOT_COUNT; n++) if (rect_has(ui_slot_rect(n), x, y)) hit = 1; }
	if (hit) dispatch(a, A_ACCEPT);
}

static int touch_menu_action(int code) {
	switch (code) {
	case ACT_UP: return A_UP;
	case ACT_DOWN: return A_DOWN;
	case ACT_LEFT: return A_LEFT;
	case ACT_RIGHT: return A_RIGHT;
	case ACT_A: return A_ACCEPT;
	case ACT_B: case ACT_PAUSE: return A_BACK;
	default: return A_NONE;
	}
}

void app_event(App* a, const AppEvent* ev) {
	switch (ev->type) {
	case EV_QUIT: a->quit = 1; break;
	case EV_KEY_DOWN: on_key_down(a, ev->code, ev->value); break;
	case EV_KEY_UP:
		set_del(a->keys_down, &a->nkeys, ev->code);
		if (key_action(ev->code) >= A_UP && key_action(ev->code) <= A_RIGHT) release_dir(a, key_action(ev->code));
		break;
	case EV_PAD_DOWN: on_pad_button(a, ev->code, 1, ev->player); break;
	case EV_PAD_UP: on_pad_button(a, ev->code, 0, ev->player); break;
	case EV_PAD_AXIS: on_pad_axis(a, ev->code, ev->value, ev->player); break;
	case EV_TOUCH_DOWN:
		if (ev->code < 0 || ev->code >= ACT_COUNT) break;
		if (a->state == ST_GAME) {
			if (ev->code == ACT_PAUSE) pause_game(a);
			else if (ev->code == ACT_QSAVE) quick_save(a);
			else if (ev->code == ACT_QLOAD) quick_load(a);
			else a->touch_down[ev->code] = 1;
		} else {
			/* En pausa/ranuras, la palanca navega y A/B confirman o vuelven. */
			int act = touch_menu_action(ev->code);
			if (act >= A_UP && act <= A_RIGHT) press_dir(a, act);
			else if (act != A_NONE) dispatch(a, act);
		}
		break;
	case EV_TOUCH_UP:
		if (ev->code >= 0 && ev->code < ACT_COUNT) {
			if (ev->code >= ACT_UP && ev->code <= ACT_RIGHT) {
				int act = touch_menu_action(ev->code);
				if (act >= A_UP && act <= A_RIGHT) release_dir(a, act);
			}
			a->touch_down[ev->code] = 0;
		}
		break;
	case EV_TOUCH_VOLUME:
		if (a->state == ST_PAUSE && !a->modal) {
			a->pause_sel = 3;
			adjust_volume(a, ev->value);
		}
		break;
	case EV_PAD_COUNT:
		a->pad_count = ev->value < 0 ? 0 : ev->value;
		if (a->pad_count < 2) { a->p2_npad = a->p2_nstick = 0; a->p2_trig[0] = a->p2_trig[1] = 0; }
		break;
	case EV_FOCUS_LOST:
		clear_inputs(a);
		if (a->state == ST_GAME) pause_game(a);
		break;
	case EV_MOUSE_MOVE: set_pad_mode(a, 0); mouse_move(a, ev->code, ev->value); break;
	case EV_MOUSE_DOWN: set_pad_mode(a, 0); mouse_click(a, ev->code, ev->value); break;
	default: break;
	}
}

/* ------------------------------------------------------------------ ciclo */
int app_update(App* a, double dt) {
	a->now += dt;
	if (a->quit) return 0;
	for (int i = 0; i < NUM_GAMES; i++) {
		float t = i == a->sel ? 1.0f : 0.0f, k = (float) (dt * 12.0);
		a->anim[i] += (t - a->anim[i]) * (k > 1 ? 1 : k);
	}
	for (int i = 0; i < PAUSE_COUNT; i++) {
		float t = (a->state == ST_PAUSE && i == a->pause_sel) ? 1.0f : 0.0f, k = (float) (dt * 14.0);
		a->pause_anim[i] += (t - a->pause_anim[i]) * (k > 1 ? 1 : k);
	}
	for (int d = 0; d < 4; d++)
		if (a->held_dir[d] && a->now >= a->held_t[d]) { dispatch(a, d); a->held_t[d] = a->now + 0.13; }

	if (a->pending_launch >= 0 && a->state == ST_MAIN) {
		if (!a->loading) a->loading = 1;                 /* dibuja un fotograma de "Iniciando…" primero */
		else { int i = a->pending_launch; a->pending_launch = -1; a->loading = 0; start_game(a, i); }
	}
	if (a->state != ST_GAME && !a->emu && a->now - a->last_scan > 1.0) app_scan_roms(a);

	if (a->state == ST_GAME && a->emu) {
		emu_set_keys(a->emu, keymask(a));
		if (a->emu2) emu_set_keys(a->emu2, keymask2(a));
		/* El turbo por frame pertenece al modo single-thread; el par enlazado tiene pacing independiente. */
		a->turbo = !a->emu2 && turbo_active(a);
		for (int k = 0; k < (a->turbo ? TURBO_FRAMES : 1); k++) {
			emu_run_frame(a->emu);
			/* J2 descarta su audio en su propio hilo; evitamos bloquear su mutex desde el hilo UI. */
			if (a->turbo) emu_clear_audio(a->emu);
			else if (a->audio_frames < 16384 - 2048)
				a->audio_frames += emu_read_audio(a->emu, a->audio + a->audio_frames * 2, 2048);
			else emu_clear_audio(a->emu);
		}
		emu_copy_video(a->emu, a->frame);
		if (a->emu2) emu_copy_video(a->emu2, a->frame2);
		a->have_frame = 1;
		a->fps_n++;
		a->fps_acc += dt;
		if (a->fps_acc >= 1.0) { a->fps = a->fps_n / a->fps_acc; a->fps_n = 0; a->fps_acc = 0; }
	}
	return !a->quit;
}

double app_frame_time(const App* a) { return a->state == ST_GAME ? 1.0 / GBA_FPS : 1.0 / 30.0; }

int app_take_audio(App* a, const int16_t** data) {
	int n = a->audio_frames;
	if (a->state != ST_GAME || n <= 0) { a->audio_frames = 0; *data = a->audio_out; return 0; }
	int vol = a->cfg.volume;
	if (!s_logged_first_audio_chunk) {
		debug_log_info("Primer bloque de audio generado: %d fotogramas de muestra, volumen de launcher=%d%%", n, vol);
		s_logged_first_audio_chunk = 1;
	}
	for (int i = 0; i < n * 2; i++) a->audio_out[i] = (int16_t) ((a->audio[i] * vol) / 100);
	a->audio_frames = 0;
	*data = a->audio_out;
	return n;
}

int app_audio_reset_requested(App* a) { int r = a->audio_reset; a->audio_reset = 0; return r; }

void app_audio_feedback(App* a, int queued_frames) {
	if (!a->emu || a->state != ST_GAME || a->host.audio_rate <= 0) return;
#if defined(__EMSCRIPTEN__)
	/* El navegador programa frames con requestAnimationFrame y puede tener
	 * pequeñas variaciones entre callbacks. Mantener ~50 ms evita underflow. */
	double target = a->host.audio_rate * 0.050;
#else
	double target = a->host.audio_rate * 0.040;
#endif
	if (a->audio_fill_avg <= 0) a->audio_fill_avg = target;
	a->audio_fill_avg += (queued_frames - a->audio_fill_avg) * 0.02;
	double err = (target - a->audio_fill_avg) / target;
	if (err < -1.0) err = -1.0;
	if (err > 1.0) err = 1.0;
	double want = 1.0 + 0.02 * err;
	a->audio_ratio += (want - a->audio_ratio) * 0.02;
	emu_set_audio_rate(a->emu, a->host.audio_rate * a->audio_ratio);
}

int app_debug_state(const App* a) { return (int) a->state; }

int app_is_paused(const App* a) { return a && a->emu && a->state == ST_PAUSE; }
int app_touch_menu_active(const App* a) { return a && a->emu && a->state != ST_GAME; }

/* Geometría en coordenadas UI (1920x1080), en la misma posición que las flechas
 * que app_ui.c dibuja en el deslizador de volumen del menú de pausa. */
int app_pause_volume_arrow_hit(const App* a, int ui_x, int ui_y) {
	if (!a || !a->emu || a->state != ST_PAUSE || a->modal) return 0;
	Rect row = ui_pause_rect(3);
	int total = 10 * 20 + 9 * 6;
	int slider_x = row.x + (row.w + 28 - 10) - total - 130;
	int cy = row.y + row.h / 2;
	if (abs(ui_y - cy) > 30) return 0;
	if (abs(ui_x - (slider_x - 24)) <= 25) return -10;
	if (abs(ui_x - (slider_x + total + 24)) <= 25) return 10;
	return 0;
}

/* ---------------------------------------------------------------- crear / destruir */
App* app_create(const char* base_dir, const AppHost* host) {
	App* a = calloc(1, sizeof(App));
	if (!a) return NULL;
	if (host) a->host = *host;
	snprintf(a->base, sizeof(a->base), "%s", base_dir);
	/* Los recursos instalados (ROMs/assets) pueden estar en una carpeta de solo lectura.
	 * Las partidas y ajustes usan user_data_dir cuando la plataforma lo proporciona. */
	const char* data_base = (host && host->user_data_dir && host->user_data_dir[0])
		? host->user_data_dir : base_dir;
	char resource_main_dir[PATH_MAX_APP], data_main_dir[PATH_MAX_APP];
	char bios_dir[PATH_MAX_APP], txt[PATH_MAX_APP + 40];
	snprintf(resource_main_dir, sizeof(resource_main_dir), "%s/main", a->base);
	snprintf(a->roms_dir, sizeof(a->roms_dir), "%s/roms", resource_main_dir);
	snprintf(data_main_dir, sizeof(data_main_dir), "%s/main", data_base);
	snprintf(a->saves_dir, sizeof(a->saves_dir), "%s/saves", data_main_dir);
	snprintf(bios_dir, sizeof(bios_dir), "%s/bios", data_main_dir);
	snprintf(a->bios_file, sizeof(a->bios_file), "%s/gba_bios.bin", bios_dir);
	snprintf(a->cfg_path, sizeof(a->cfg_path), "%s/config.txt", data_main_dir);
	snprintf(a->assets_dir, sizeof(a->assets_dir), "%s/assets", a->base);
	snprintf(a->games_art_dir, sizeof(a->games_art_dir), "%s/games", a->assets_dir);
	/* ROMs/assets vienen empaquetados; intentar mkdir en ellos es inocuo si /app0 es read-only. */
	sys_mkdir_p(a->roms_dir); sys_mkdir_p(a->saves_dir); sys_mkdir_p(bios_dir); sys_mkdir_p(a->games_art_dir);
	snprintf(txt, sizeof(txt), "%s/NOMBRES_DE_LAS_ROMS.txt", a->roms_dir);
	write_if_missing(txt, NAMES_TXT);
	snprintf(txt, sizeof(txt), "%s/LEEME.txt", bios_dir);
	write_if_missing(txt, BIOS_TXT);

	app_load_config(a);
	a->pending_launch = -1;
	a->prompt_game = -1;
	a->capture_kind = -1;
	a->stick_dir = -1;
	a->delete_slot = -1;
	a->last_slot = 1;
	a->game_idx = 0;
	for (int i = 0; i < NUM_GAMES; i++) a->anim[i] = i == 0 ? 1.0f : 0.0f;
	debug_log_info("app_create: base=%s; ROMs=%s; assets=%s", a->base, a->roms_dir, a->assets_dir);
	debug_log_info("app_create: escaneando ROMs");
	app_scan_roms(a);
	debug_log_info("app_create: escaneo ROMs terminado");
	a->ui = cv_new(UI_W, UI_H);
	if (!a->ui) {
		debug_log_error("app_create: no hay memoria para el lienzo de UI (%dx%d)", UI_W, UI_H);
		free(a);
		return NULL;
	}
	debug_log_info("app_create: cargando y preparando assets de UI");
	ui_load_assets(a);
	debug_log_info("app_create: assets de UI preparados");
	if (a->cfg.fullscreen && a->host.set_fullscreen) a->host.set_fullscreen(a->host.user, 1);
	return a;
}

void app_destroy(App* a) {
	if (!a) return;
	if (a->emu) emu_close(a->emu);
	a->emu = a->emu2 = NULL;
	for (int n = 1; n <= SLOT_COUNT; n++) cv_free(a->slot_thumbs[n]);
	ui_free_assets(a);
	cv_free(a->ui);
	free(a);
}
