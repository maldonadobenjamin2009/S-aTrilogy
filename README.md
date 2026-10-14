# Sonic Advance Trilogy – Launcher (C + SDL2 + mGBA integrado)

Launcher estilo "GTA Trilogy" para Sonic Advance 1-3. **Un solo ejecutable**: el núcleo de mGBA está
compilado dentro del programa (nada de DLLs aparte, Python ni compilar mGBA por separado).

```
 plataforma (main_sdl.c)  <-- eventos / fotograma / audio -->  launcher_core  -->  emu.c  -->  mGBA (C, sin modificar)
 SDL2: ventana, mando, sonido                                  app_*.c, gfx.c, png.c
```

* `src/app_logic.c`, `src/app_ui.c` – estados, menús, pausa, ranuras, remapeo de controles (portable).
* `src/gfx.c`, `src/png.c`, `src/font_data.c` – interfaz dibujada **por software** (sin dependencias); la
  tipografía va incrustada (atlas SDF generado por `tools/gen_font.py`).
* `src/emu.c` – integración de mGBA y enlace real por cable link entre dos núcleos GBA.
* `src/main_sdl.c` – única parte que depende de SDL2 (PC), con identificación de los dos mandos y renderizado lado a lado: J1 a la izquierda y J2 a la derecha. Android/PS4 necesitan su propia versión de este archivo (ver `docs/PORTADO.md`).
* `third_party/mgba` – núcleo GBA de mGBA (MPL 2.0), con soporte de hilos/proxy, un ajuste de cierre del coordinador de cable link y retornos de sincronización corregidos para Windows.

## Multijugador local

Al iniciar una ROM con dos mandos reconocidos, aparece la pregunta «Se ha detectado otro mando más conectado, ¿quieres jugar a pantalla dividida?». **Sí** abre dos núcleos de la misma ROM conectados inmediatamente mediante el coordinador de cable link de mGBA; **No** conserva la partida de un jugador. J1 se dibuja a la izquierda y J2 a la derecha. El audio que se escucha es únicamente el de J1. Cada jugador usa su propio mando; J2 no controla los menús de pausa. Los guardados de estado en multijugador guardan ambos núcleos: el segundo archivo se añade con el sufijo `.p2` al nombre de la ranura. El archivo de guardado normal de batería de J2 se guarda aparte con sufijo `.p2.sav`.

## Diagnóstico en Windows

En Windows, el ejecutable abre una consola de depuración al arrancar. El registro `main_errors.log` se guarda junto al ejecutable (si esa carpeta no permite escritura, se intenta en el directorio de trabajo). Incluye errores de SDL/audio, detección de mandos, arranque de ROMs y avisos/errores emitidos por mGBA. Este registro es útil para adjuntar al reportar fallos al entrar en pantalla dividida.

La capa de hilos de Windows incluida con mGBA también comprueba el valor de retorno real de las esperas de condición y la creación de hilos; evita interpretar un `GetLastError()` antiguo como un error de sincronización.

## Compilar

**Sin instalar nada (recomendado):** sube la carpeta a GitHub; `.github/workflows/build.yml` compila
Windows, Linux y macOS y deja un `.zip` por sistema en *Actions → artefactos*.

**En tu PC** (CMake ≥ 3.16 y un compilador de C):

```
# Linux:   sudo apt install build-essential cmake libsdl2-dev     (Arch: sudo pacman -S base-devel cmake sdl2)
# macOS:   brew install cmake sdl2
# Windows: MSYS2 MINGW64 -> pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja mingw-w64-x86_64-SDL2
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
cmake --install build --prefix SonicAdvanceTrilogy      # ejecutable + carpetas listas para usar
```

Sin CMake (Linux/macOS/MSYS2): `make` (necesita `sdl2-config` o `pkg-config`). Prueba sin ventana: `make test && bin/headless_test /tmp/trabajo /tmp/capturas`
(ejecuta el mGBA real con una ROM de prueba mínima y guarda capturas PNG de cada pantalla).

## Android (APK)

Hay un proyecto Android Studio en `android/` con Gradle + NDK + CMake. Abre esa carpeta en Android Studio para compilar el APK; en el primer sync se descargan las fuentes oficiales de SDL2 2.30.9. También existe un workflow `.github/workflows/android.yml` que compila y publica el APK debug como artefacto de Actions.

La versión Android se ejecuta en horizontal e incluye controles táctiles semitransparentes: cruceta izquierda, A/B a la derecha, L/R en las esquinas superiores, botón de pausa en el centro superior y SELECT/START abajo al centro. Los controles táctiles emiten eventos SDL de mando, y los mandos Bluetooth siguen disponibles. La compilación Android empaqueta `dist/main/roms/` y `dist/assets/` dentro del APK y copia los recursos que falten a los datos privados de la aplicación en el primer inicio; las instrucciones completas están en `android/README.md`.

**Limitación de esta revisión:** no se generó un APK aquí porque este entorno no tiene Android SDK/NDK ni acceso a la descarga inicial de SDL2. El proyecto Android Studio y su workflow de compilación quedaron añadidos, pero es necesario validar la primera compilación con el SDK real.


## PS4 PKG (homebrew con OpenOrbis)

Hay un flujo experimental de compilación de un paquete `.pkg` usando el toolchain abierto OpenOrbis y su paquete SDL2 para PS4. El workflow `.github/workflows/ps4-pkg.yml` se puede iniciar desde **Actions → PS4 PKG (OpenOrbis homebrew) → Run workflow**; también se dispara al cambiar código, `dist/` o `ps4/` en `main`/`master`. Debe tener disponibles en el checkout las ROMs propias en `dist/main/roms/` y los recursos en `dist/assets/` (se admite Git LFS).

La compilación produce `build-ps4/SonicAdvanceTrilogy-PS4.pkg` como artefacto de Actions. El PKG es una compilación homebrew de desarrollo; no es un paquete comercial firmado por Sony ni se espera que instale en una PS4 retail sin soporte homebrew. No incluye métodos para modificar el firmware de la consola.

El frontend conserva los recursos de solo lectura en la carpeta del paquete (`/app0/main/roms`, `/app0/assets`) y guarda configuración, BIOS opcional y partidas bajo `/data/SonicAdvanceTrilogy/main/`. Se reutiliza SDL2 de OpenOrbis y el empaquetador `create-fself`/`create-gp4`/`PkgTool.Core` del toolchain. Este entorno de desarrollo no dispone del SDK de OpenOrbis, por lo que el nuevo target todavía necesita validación mediante una ejecución real de Actions; el flujo comprobará dependencias y recursos faltantes y fallará mostrando el error de compilación concreto.

## Estado de las pruebas

Probado en Linux x86-64: núcleo, interfaz, pausa, ranuras, remapeo, aviso Sí/No para dos mandos y flujo de pantalla dividida con `headless_test`. También se ejecutó una prueba directa de cable link que avanza dos núcleos, guarda/carga sus estados y cierra las instancias.
**Limitación de esta revisión:** el entorno de desarrollo no tiene SDL2 instalado, por lo que el ejecutable gráfico `main_sdl.c` no se ha podido enlazar ni probar aquí con SDL2 real. Tampoco se validaron en este entorno las compilaciones de Windows/macOS ni el flujo de GitHub Actions.
