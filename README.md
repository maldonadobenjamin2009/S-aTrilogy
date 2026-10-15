# Corrección PS4: pantalla negra y 0 FPS

Estos son los dos archivos modificados para aplicar el arreglo sobre el repositorio actual:

- `src/main_sdl.c`
- `src/debug_log.c`

Cambios específicos de PS4/OpenOrbis:
- Usa `SDL_RENDERER_SOFTWARE` explícitamente en PS4, igual que el ejemplo SDL2 de OpenOrbis, en vez de seleccionar un renderer acelerado que puede inicializar sin presentar imagen en ciertas builds.
- Consulta `sceVideoOutGetResolutionStatus()` para crear la ventana a la resolución real de la consola.
- Inicializa el subsistema SDL timer y usa `SDL_GetTicks()` para el tiempo y pacing del bucle PS4, evitando depender de `SDL_GetPerformanceFrequency()`.
- Elimina el spin-wait infinito del bucle PS4 y registra FPS cada 5 segundos.
- Escribe el log en `/data/SonicAdvanceTrilogy/main_errors.log` para que no intente escribir en `/app0` (solo lectura).

Aplicar el parche es preferible a reemplazar archivos enteros:

```bash
git apply fix-ps4-black-screen-renderer-timing.patch
git diff --check
make test
```

No se ha podido compilar ni ejecutar en hardware PS4 en este entorno. La suite headless sí terminó con 75 comprobaciones correctas y 0 fallos.
