Sonic Advance Trilogy — PS4 SDL window-surface presentation fix

Included file:
  src/main_sdl.c

Apply the accompanying patch from the root of your existing private repository:
  git apply fix-ps4-window-surface-present.patch
  git diff --check

This change follows the official OpenOrbis SDL2 sample's presentation path:
SDL_GetWindowSurface + SDL_CreateSoftwareRenderer + SDL_UpdateWindowSurface.
It does not expect GPU/VRAM activity: the SDL software renderer draws on CPU and
updates the window surface. The goal is to get actual frames visible on PS4.

A complete PS4 toolchain build and execution on real hardware were not available
in the authoring environment, so please inspect main_errors.log after testing.
