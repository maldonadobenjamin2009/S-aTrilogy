#include "config.h"
#include "keys.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char* const action_label[ACT_COUNT] = {
	"Botón A", "Botón B", "Botón L", "Botón R", "Start", "Select", "Arriba", "Abajo",
	"Izquierda", "Derecha", "Pausa", "Acelerar (mantener)", "Guardado rápido", "Carga rápida",
};
static const char* const action_id[ACT_COUNT] = {
	"A", "B", "L", "R", "START", "SELECT", "UP", "DOWN", "LEFT", "RIGHT", "PAUSE", "TURBO", "QSAVE", "QLOAD",
};

void config_defaults(Config* c) {
	memset(c, 0, sizeof(*c));
	c->fullscreen = 0; c->volume = 80; c->smooth = 0; c->integer_scale = 0; c->show_fps = 0;
	const int k[ACT_COUNT] = {'x', 'z', 'a', 's', K_RETURN, K_BACKSPACE, K_UP, K_DOWN, K_LEFT, K_RIGHT,
	                          K_ESCAPE, K_TAB, K_F5, K_F8};
	const int p[ACT_COUNT] = {PAD_CIRCLE, PAD_CROSS, PAD_L1, PAD_R1, PAD_OPTIONS, PAD_SHARE, PAD_UP, PAD_DOWN,
	                          PAD_LEFT, PAD_RIGHT, PAD_PS, PAD_R2, -1, -1};
	for (int i = 0; i < ACT_COUNT; i++) { c->keys[i] = k[i]; c->pad[i] = p[i]; }
}

int config_load(Config* c, const char* path) {
	FILE* f = fopen(path, "r");
	if (!f) return 0;
	char line[256];
	while (fgets(line, sizeof(line), f)) {
		char* eq = strchr(line, '=');
		if (!eq) continue;
		*eq = 0;
		const char* key = line;
		long v = strtol(eq + 1, NULL, 10);
		if (!strcmp(key, "fullscreen")) c->fullscreen = v != 0;
		else if (!strcmp(key, "volume")) c->volume = v < 0 ? 0 : (v > 100 ? 100 : (int) v);
		else if (!strcmp(key, "smooth")) c->smooth = v != 0;
		else if (!strcmp(key, "integer_scale")) c->integer_scale = v != 0;
		else if (!strcmp(key, "show_fps")) c->show_fps = v != 0;
		else if (!strncmp(key, "key_", 4) || !strncmp(key, "pad_", 4)) {
			for (int i = 0; i < ACT_COUNT; i++)
				if (!strcmp(key + 4, action_id[i])) { if (key[0] == 'k') c->keys[i] = (int) v; else c->pad[i] = (int) v; }
		}
	}
	fclose(f);
	return 1;
}

int config_save(const Config* c, const char* path) {
	FILE* f = fopen(path, "w");
	if (!f) return 0;
	fprintf(f, "fullscreen=%d\nvolume=%d\nsmooth=%d\ninteger_scale=%d\nshow_fps=%d\n", c->fullscreen, c->volume, c->smooth, c->integer_scale, c->show_fps);
	for (int i = 0; i < ACT_COUNT; i++) fprintf(f, "key_%s=%d\n", action_id[i], c->keys[i]);
	for (int i = 0; i < ACT_COUNT; i++) fprintf(f, "pad_%s=%d\n", action_id[i], c->pad[i]);
	return fclose(f) == 0;
}
