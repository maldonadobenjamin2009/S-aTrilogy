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

## Android (implementado en `android/`)

- Proyecto Android Studio con Gradle, NDK y CMake; el núcleo C y mGBA se compilan en `libmain.so` y SDL2.
- SDL2 oficial 2.30.9 se descarga al sincronizar Gradle si no se ha colocado previamente en `android/SDL2/`.
- `src/main_sdl.c` usa la carpeta de archivos de la app para `main/roms`, `main/saves` y `config.txt`.
- La app se fuerza a horizontal y dibuja controles táctiles semitransparentes: cruceta izquierda, A/B a la derecha, L/R arriba, pausa centrada arriba y SELECT/START centrados abajo.
- El toque se traduce a acciones GBA virtuales separadas del mapeo físico, por lo que remapear un mando Bluetooth no altera la cruceta táctil.
- El modo de pantalla dividida sigue disponible con dos mandos compatibles; los controles táctiles sirven a J1.
- Las instrucciones para compilar e importar ROMs están en `android/README.md` y el workflow `.github/workflows/android.yml` produce un APK debug como artefacto.

**Pendiente de validación física:** este entorno no tiene Android SDK/NDK ni conexión a Internet para descargar SDL2, así que el APK aún debe compilarse y probarse desde Android Studio o GitHub Actions.

## PS4 (homebrew, requiere consola con jailbreak)
* Toolchain: OpenOrbis (C/C++, clang). `launcher_core` y mGBA no usan hilos (`DISABLE_THREADING`) ni librerías externas.
* Escribe `platform_ps4.c`: vídeo con `sceVideoOut` (el lienzo ya es un buffer ARGB por software, solo hay que copiarlo
  y escalar el fotograma del juego), mando con `scePad` (mapea sus bits a los `PAD_*` de `keys.h`: Cruz→0, Círculo→1,
  Cuadrado→2, Triángulo→3, Share→4, Options→6, L1→9, R1→10, cruceta→11-14, L2/R2→100/101 o eje 4/5, panel táctil→20),
  audio con `sceAudioOut` (48 kHz s16 estéreo) y rutas bajo `/data/...`.
* Compila las fuentes de mGBA con las mismas `-D` del Makefile. Sin probar: la PS4 no se ha tocado todavía.
