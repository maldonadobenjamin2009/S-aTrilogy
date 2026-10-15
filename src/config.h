#ifndef CONFIG_H
#define CONFIG_H

enum { ACT_A, ACT_B, ACT_L, ACT_R, ACT_START, ACT_SELECT, ACT_UP, ACT_DOWN, ACT_LEFT, ACT_RIGHT,
       ACT_PAUSE, ACT_TURBO, ACT_QSAVE, ACT_QLOAD, ACT_COUNT };

typedef struct {
	int fullscreen;
	int volume;          /* 0..100 */
	int smooth;          /* suavizado de imagen en el juego */
	int integer_scale;
	int show_fps;
	int keys[ACT_COUNT]; /* código de tecla o -1 */
	int pad[2][ACT_COUNT]; /* asignaciones independientes de mando para J1 y J2 */
} Config;

extern const char* const action_label[ACT_COUNT];

void config_defaults(Config* c);
int config_load(Config* c, const char* path);
int config_save(const Config* c, const char* path);

#endif
