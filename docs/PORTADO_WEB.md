# Port web de Sonic Advance Trilogy (Emscripten)

Esta adaptación no sustituye la aplicación por otro frontend de emulación. Compila el proyecto C/C++ existente: `src/main_sdl.c`, `src/app_logic.c`, `src/app_ui.c`, `src/gfx.c`, `src/emu.c` y el núcleo mGBA incluido en `third_party/mgba/`.

## Cómo funciona

- Emscripten compila el ejecutable SDL2 a WebAssembly y genera `index.html`, `index.js`, `index.wasm` e `index.data`.
- `web/shell.html` es la plantilla HTML del compilador, y `web/launcher.js` conecta los selectores de archivos con el sistema de archivos virtual de Emscripten.
- La compilación carga únicamente `dist/assets` como recursos de la interfaz. No empaqueta la carpeta `dist/main/roms` ni publica ROMs.
- Los archivos elegidos por el usuario se copian a `/main/roms/sonicadvance.gba`, `/main/roms/sonicadvance2.gba` y `/main/roms/sonicadvance3.gba`, que son las rutas que ya busca el launcher C. Se copian localmente en IndexedDB para que no haya que volver a elegirlos cada vez.
- Las partidas, ranuras y configuración se escriben bajo `/persistent/main/`, montado con IDBFS; se sincronizan periódicamente con el navegador.
- El bucle de SDL2 cede el hilo al navegador mediante `emscripten_sleep`/Asyncify para que Chrome pueda procesar eventos, audio y repintado.

## Publicar en GitHub Pages

1. Copiá los archivos modificados de este paquete en la raíz del repositorio: `CMakeLists.txt`, los tres archivos `src/` modificados, `web/`, `.github/workflows/web-pages.yml` y este documento.
2. En GitHub, abrí **Settings → Pages** y seleccioná **GitHub Actions** como origen del despliegue.
3. Confirmá los cambios en la rama `main`. El workflow instala Emscripten, compila el proyecto C, prepara el sitio y lo publica.
4. Revisá **Actions → Build and deploy Sonic Advance Trilogy WebAssembly**. La web no estará lista hasta que el job de compilación termine correctamente.

No abras el shell `web/shell.html` directamente desde el disco. La plantilla se procesa durante la compilación; probá el resultado por un servidor HTTP o por GitHub Pages.

## Límites del primer port web

El núcleo de juego y el hub se ejecutan en un único hilo para ser compatibles con un alojamiento estático típico como GitHub Pages. Por eso se omite el modo de pantalla dividida/cable link que usa dos hilos mGBA. Teclado, SDL GameController, emulación individual, audio, menús, remapeo de controles y ranuras de guardado permanecen implementados por el código original siempre que el backend web de SDL2 lo soporte.

El compilador Emscripten y SDL2 para WebAssembly son necesarios para generar los artefactos. Si Actions encuentra una incompatibilidad del toolchain en el código mGBA incluido, el log del job indicará el siguiente arreglo; no se debe reemplazar el núcleo por otro emulador para solventarla.

## Seguridad y archivos personales

El workflow carga en Pages solamente los recursos de la interfaz y artefactos compilados; no pasa ninguna ROM a `--preload-file`. Importá únicamente ROMs que tengas derecho a usar. Los archivos que elegís desde la página se guardan en el almacenamiento local de tu propio navegador.
