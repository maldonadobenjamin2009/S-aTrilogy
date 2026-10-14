#ifndef KEYS_H
#define KEYS_H
/* Códigos de teclado del launcher. Coinciden con los SDLK_* de SDL2 para que el soporte de
 * escritorio y Android sea directo; otras plataformas (PS4...) traducen a estos valores. */
#define K_BACKSPACE 8
#define K_TAB 9
#define K_RETURN 13
#define K_ESCAPE 27
#define K_SPACE 32
#define K_DELETE 127
#define K_RIGHT 1073741903
#define K_LEFT 1073741904
#define K_DOWN 1073741905
#define K_UP 1073741906
#define K_F1 1073741882
#define K_F5 1073741886
#define K_F8 1073741889
#define K_F11 1073741892

/* Ids de botones de mando = SDL_GameControllerButton (PS4: 0 Cruz, 1 Círculo, 2 Cuadrado,
 * 3 Triángulo, 4 Share, 5 PS, 6 Options, 7 L3, 8 R3, 9 L1, 10 R1, 11-14 cruceta, 20 panel táctil)
 * más 100 = L2 y 101 = R2 (gatillos como botón). */
#define PAD_CROSS 0
#define PAD_CIRCLE 1
#define PAD_SQUARE 2
#define PAD_TRIANGLE 3
#define PAD_SHARE 4
#define PAD_PS 5
#define PAD_OPTIONS 6
#define PAD_TOUCHPAD 20
#define PAD_L1 9
#define PAD_R1 10
#define PAD_UP 11
#define PAD_DOWN 12
#define PAD_LEFT 13
#define PAD_RIGHT 14
#define PAD_L2 100
#define PAD_R2 101
#endif
