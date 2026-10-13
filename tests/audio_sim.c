/* Simula un dispositivo de audio REAL (pide bloques fijos con su propio reloj desviado) y un bucle principal con
 * jitter de planificación (como Windows). Usa el mGBA real. Cuenta cortes = bloques en los que faltó audio. */
#include "../src/app_internal.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void write_rom(const char* path);
static App* app;
static unsigned rng = 12345;
static double frand(void) { rng = rng * 1664525u + 1013904223u; return (rng >> 8) / 16777216.0; }

typedef struct { double drift; int chunk; double jitter_ms; } Scn;

static void run(const char* name, const Scn* s, int preroll_ms) {
	const double rate = 48000.0, dev_rate = rate * (1.0 + s->drift);
	double queued = rate * preroll_ms / 1000.0;           /* en fotogramas */
	double t = 0, next_sched = 0, next_exec = 0, next_pull = s->chunk / dev_rate;
	long underruns = 0, pulls = 0, frames = 0, late = 0;
	double qmin = 1e9, qmax = 0, qsum = 0, rmin = 9, rmax = 0; long qn = 0;
	AppEvent e = {EV_KEY_DOWN, K_RETURN, 0};
	app_event(app, &e);
	for (int i = 0; i < 4 && app_debug_state(app) != ST_GAME; i++) app_update(app, 0.016);
	app_audio_reset_requested(app);
	const double TICK = 0.0002;
	for (long step = 0; step < 100000; step++) {         /* 20 s simulados */
		t += TICK;
		while (t >= next_pull) {                         /* el dispositivo pide un bloque */
			pulls++;
			if (queued < s->chunk) { if (t > 1.0) underruns++; queued = 0; } else queued -= s->chunk;
			next_pull += s->chunk / dev_rate;
		}
		if (t > 1.0) { if (queued < qmin) qmin = queued; if (queued > qmax) qmax = queued; qsum += queued; qn++; }
		if (t >= next_exec) {                            /* el bucle principal avanza un fotograma */
			app_audio_feedback(app, (int) queued);
			app_update(app, 1.0 / GBA_FPS);
			frames++;
			const int16_t* d;
			int n = app_take_audio(app, &d);
			queued += n;
			if (queued > rate * 0.4) queued = rate * 0.4;      /* tope de seguridad */
			if (t > 1.0) { if (app->audio_ratio < rmin) rmin = app->audio_ratio; if (app->audio_ratio > rmax) rmax = app->audio_ratio; }
			next_sched += 1.0 / GBA_FPS;
			double jit = (frand() < 0.02 ? 6.0 + 6.0 * frand() : frand() * s->jitter_ms) / 1000.0;   /* a veces se retrasa mucho */
			if (jit > 0.004) late++;
			next_exec = next_sched + jit;
			if (next_exec < t) next_exec = t;
		}
	}
	printf("%-26s deriva %+4.1f%% bloque %4d jitter %3.1fms | cola mín %3.0f media %3.0f máx %3.0f ms | ratio %.4f-%.4f | CORTES %4ld de %ld (%.1f%%)\n",
	       name, s->drift * 100, s->chunk, s->jitter_ms, qmin / rate * 1000, qsum / qn / rate * 1000, qmax / rate * 1000, rmin, rmax,
	       underruns, pulls, 100.0 * underruns / pulls);
}

int main(int argc, char** argv) {
	const char* work = argc > 1 ? argv[1] : "asim_work";
	int preroll = argc > 2 ? atoi(argv[2]) : 0;
	char p[600];
	snprintf(p, sizeof(p), "%s/main/roms", work); sys_mkdir_p(p);
	snprintf(p, sizeof(p), "%s/main/roms/SonicAdvance.GBA", work);
	write_rom(p);
	AppHost host = {0};
	host.audio_rate = 48000; host.has_audio = 1;
	Scn sc[] = {{0.0, 512, 1.5}, {0.003, 512, 1.5}, {-0.003, 512, 1.5}, {0.0, 1024, 1.5}, {0.003, 1024, 3.0}, {-0.005, 1024, 3.0}};
	for (unsigned i = 0; i < sizeof(sc) / sizeof(sc[0]); i++) {
		rng = 12345;
		app = app_create(work, &host);
		run(argc > 3 ? argv[3] : "diseño actual", &sc[i], preroll);
		app_destroy(app);
	}
	return 0;
}

static void write_rom(const char* path) {
	static const uint32_t code[] = {0xE3A00301, 0xE3A01901, 0xE3811C02, 0xE1C018B8, 0xE3A01080, 0xE1C018B4, 0xE3A01CFF, 0xE3811077, 0xE1C018B0, 0xE3A01002, 0xE1C018B2, 0xE3A01CF0, 0xE3811080, 0xE1C016B2, 0xE3A01902, 0xE3811C07, 0xE1C016B4, 0xE3A01B01, 0xE3811003, 0xE5801000, 0xE3A02406, 0xE3A0301F, 0xE3A05C96, 0xE0C230B2, 0xE2555001, 0x1AFFFFFC, 0xEAFFFFFE};
	uint8_t rom[0x1000]; memset(rom, 0, sizeof(rom));
	rom[0] = 0x2E; rom[3] = 0xEA; memcpy(rom + 0xA0, "TESTROM", 7); memcpy(rom + 0xAC, "ATST", 4); memcpy(rom + 0xB0, "01", 2); rom[0xB2] = 0x96;
	int chk = 0; for (int i = 0xA0; i < 0xBD; i++) chk -= rom[i]; rom[0xBD] = (uint8_t) (chk - 0x19);
	for (size_t i = 0; i < sizeof(code) / 4; i++) for (int b = 0; b < 4; b++) rom[0xC0 + i * 4 + b] = (uint8_t) (code[i] >> (8 * b));
	FILE* f = fopen(path, "wb"); if (f) { fwrite(rom, 1, sizeof(rom), f); fclose(f); }
}
