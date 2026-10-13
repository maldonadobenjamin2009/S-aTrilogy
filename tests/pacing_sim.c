/* Simula el dispositivo de audio (consume a su propio reloj, con desvío) y compara dos formas de marcar
 * el ritmo del emulador: por reloj (versión anterior) y por audio (actual). Usa el mGBA real. */
#include "../src/app_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void write_rom(const char* path);
static App* app;
static void key(int k) { AppEvent e = {EV_KEY_DOWN, k, 0}; app_event(app, &e); e.type = EV_KEY_UP; app_event(app, &e); }

/* modo 0: por reloj, descartando si la cola pasa de 140 ms (primera versión)
 * modo 1: el audio marca el ritmo (versión anterior)
 * modo 2: por reloj + control dinámico de la tasa de audio (actual) */
static void run(const char* name, int mode, double drift) {
	const double rate = 48000.0, dev_rate = rate * (1.0 + drift);
	const double target = rate * 4 * 0.040, maxq = rate * 4 * 0.250;
	double queued = 0, dev_acc = 0, t = 0, next_frame = 0;
	long underruns = 0, drops = 0, frames = 0; double qmin = 1e9, qmax = 0, qsum = 0; long qn = 0;
	const double TICK = 0.001;
	AppEvent e = {EV_KEY_DOWN, K_RETURN, 0};
	app_event(app, &e);
	for (int i = 0; i < 4 && app_debug_state(app) != ST_GAME; i++) app_update(app, 0.016);
	app_audio_reset_requested(app);
	for (int step = 0; step < 20000; step++) {          /* 20 s simulados */
		t += TICK;
		dev_acc += dev_rate * TICK;
		double take = (double) (long) dev_acc * 4;
		dev_acc -= (long) dev_acc;
		if (take > queued) { if (take > 0) underruns++; take = queued; }
		queued -= take;
		if (step > 500) { if (queued < qmin) qmin = queued; if (queued > qmax) qmax = queued; qsum += queued; qn++; }
		int run_frame;
		if (mode == 1) run_frame = queued < target;
		else { run_frame = t >= next_frame; if (run_frame) next_frame += 1.0 / GBA_FPS; }
		if (!run_frame) continue;
		if (mode == 2) { if (queued > maxq) { queued = 0; drops++; } app_audio_feedback(app, (int) (queued / 4)); }
		app_update(app, 1.0 / GBA_FPS);
		frames++;
		const int16_t* d;
		int n = app_take_audio(app, &d);
		if (mode == 0 && queued > rate * 4 * 0.140) { drops++; continue; }
		queued += n * 4.0;
	}
	printf("%-30s dispositivo %+5.1f%%: velocidad %5.1f%% | cola mín %4.0f máx %4.0f ms | cortes %4ld  descartes %3ld\n",
	       name, drift * 100, frames / (20.0 * GBA_FPS) * 100, qmin / (rate * 4) * 1000, qmax / (rate * 4) * 1000, underruns, drops);
}

int main(int argc, char** argv) {
	const char* work = argc > 1 ? argv[1] : "pace_work";
	char p[600];
	snprintf(p, sizeof(p), "%s/main/roms", work); sys_mkdir_p(p);
	snprintf(p, sizeof(p), "%s/main/roms/SonicAdvance.GBA", work);
	write_rom(p);
	AppHost host = {0};
	host.audio_rate = 48000; host.has_audio = 1;
	double drifts[] = {-0.5, -0.003, 0.0, 0.003};
	const char* names[3] = {"1) reloj + descartes (inicial)", "2) audio manda (anterior)", "3) reloj + control (actual)"};
	for (int m = 0; m < 3; m++) {
		for (int i = 0; i < 4; i++) {
			app = app_create(work, &host);
			run(names[m], m, drifts[i]);
			app_destroy(app);
		}
		printf("\n");
	}
	return 0;
}

static void write_rom(const char* path) {
	static const uint32_t code[] = {0xE3A00301, 0xE3A01B01, 0xE3811003, 0xE5801000, 0xE3A02406, 0xE3A0301F, 0xE3A05C96, 0xE0C230B2, 0xE2555001, 0x1AFFFFFC, 0xEAFFFFFE};
	uint8_t rom[0x1000]; memset(rom, 0, sizeof(rom));
	rom[0] = 0x2E; rom[3] = 0xEA; memcpy(rom + 0xA0, "TESTROM", 7); memcpy(rom + 0xAC, "ATST", 4); memcpy(rom + 0xB0, "01", 2); rom[0xB2] = 0x96;
	int chk = 0; for (int i = 0xA0; i < 0xBD; i++) chk -= rom[i]; rom[0xBD] = (uint8_t) (chk - 0x19);
	for (size_t i = 0; i < sizeof(code) / 4; i++) for (int b = 0; b < 4; b++) rom[0xC0 + i * 4 + b] = (uint8_t) (code[i] >> (8 * b));
	FILE* f = fopen(path, "wb"); if (f) { fwrite(rom, 1, sizeof(rom), f); fclose(f); }
}
