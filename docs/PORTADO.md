# Portar a Android y PS4

Toda la lógica (menús, pausa, ranuras, controles, emulación) está en `launcher_core` y es C estándar sin
llamadas al sistema operativo. Cada plataforma solo implementa un **archivo de plataforma** que cumple este contrato
(`src/app.h`):

| La plataforma… | …llama a |
|---|---|
| traduce sus eventos | `app_event(app, &ev)` con `EV_KEY_*`, `EV_PAD_*` (ids de `keys.h`), `EV_PAD_AXIS`, `EV_MOUSE_*`, `EV_FOCUS_LOST`, `EV_QUIT` |
| cada fotograma | `app_update(app, dt)` y `app_render(app, &frame)` |
| muestra | `frame.game` (240x160, bytes R,G,B,X) escalado + `frame.ui` (1920x1080 ARGB; capa con transparencia durante el juego) |
| reproduce audio | `app_take_audio()` (PCM s16 estéreo a `host.audio_rate`); si `app_audio_reset_requested()` vacía la cola |
| respeta el ritmo | `app_frame_time()` (≈1/59,7275 s en juego, 1/30 s en menús) |
| ofrece al arrancar | `AppHost`: abrir carpeta, pantalla completa, título, cursor, nombre de tecla |

## Android
* Plantilla oficial de SDL2 para Android (`android-project`): `main_sdl.c` ya usa solo SDL2 (ventana, audio, mando, teclado),
  así que sirve casi sin cambios. Añade `src/*.c` y `third_party/mgba` al `CMakeLists.txt` de la plantilla (mismas
  fuentes y `-D` que el `CMakeLists.txt` de la raíz; el NDK usa clang).
* Cambia la carpeta base a `SDL_AndroidGetExternalStoragePath()` (ahí van `main/roms`, `main/saves`…).
* Falta un **mando táctil**: dibuja botones en el lienzo `ui` y envía `EV_PAD_DOWN/UP` con los ids de `keys.h`.
* Los mandos Bluetooth (DualShock 4 incluido) ya funcionan por la API de SDL_GameController.

## PS4 (homebrew, requiere consola con jailbreak)
* Toolchain: OpenOrbis (C/C++, clang). `launcher_core` y mGBA no usan hilos (`DISABLE_THREADING`) ni librerías externas.
* Escribe `platform_ps4.c`: vídeo con `sceVideoOut` (el lienzo ya es un buffer ARGB por software, solo hay que copiarlo
  y escalar el fotograma del juego), mando con `scePad` (mapea sus bits a los `PAD_*` de `keys.h`: Cruz→0, Círculo→1,
  Cuadrado→2, Triángulo→3, Share→4, Options→6, L1→9, R1→10, cruceta→11-14, L2/R2→100/101 o eje 4/5, panel táctil→20),
  audio con `sceAudioOut` (48 kHz s16 estéreo) y rutas bajo `/data/...`.
* Compila las fuentes de mGBA con las mismas `-D` del Makefile. Sin probar: la PS4 no se ha tocado todavía.
