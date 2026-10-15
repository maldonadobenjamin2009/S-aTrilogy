/* Minimal PS4 SDL smoke test based on J-D-K/PS4-SDL-Template.
 * Excludes mGBA and launcher resources to isolate executable + SDL startup. */
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <orbis/VideoOut.h>
#include <string.h>
#include "debug_log.h"

int main(int argc, char** argv) {
    (void) argc; (void) argv;
    debug_log_init();
    debug_log_info("PS4 SMOKE: entered main()");
    SDL_SetMainReady();
    int width = 1920, height = 1080;
    int video = sceVideoOutOpen(ORBIS_VIDEO_USER_MAIN, ORBIS_VIDEO_OUT_BUS_MAIN, 0, 0);
    if (video < 0) {
        debug_log_error("PS4 SMOKE: sceVideoOutOpen failed: 0x%x", video);
    } else {
        OrbisVideoOutResolutionStatus status;
        memset(&status, 0, sizeof(status));
        int rc = sceVideoOutGetResolutionStatus(video, &status);
        if (rc < 0) debug_log_error("PS4 SMOKE: get resolution failed: 0x%x", rc);
        else if (status.width > 0 && status.height > 0) { width = (int) status.width; height = (int) status.height; }
        (void) sceVideoOutClose(video);
    }
    debug_log_info("PS4 SMOKE: resolution=%dx%d", width, height);
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) != 0) {
        debug_log_error("PS4 SMOKE: SDL_Init failed: %s", SDL_GetError());
        debug_log_close(); return 10;
    }
    debug_log_info("PS4 SMOKE: SDL video driver=%s", SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "(null)");
    SDL_Window* win = SDL_CreateWindow("SAT PS4 Smoke Test", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height, SDL_WINDOW_FULLSCREEN);
    if (!win) {
        debug_log_error("PS4 SMOKE: SDL_CreateWindow failed: %s", SDL_GetError());
        SDL_Quit(); debug_log_close(); return 11;
    }
    debug_log_info("PS4 SMOKE: window created");
    SDL_Renderer* renderer = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!renderer) {
        debug_log_error("PS4 SMOKE: SDL_CreateRenderer(SOFTWARE) failed: %s", SDL_GetError());
        SDL_DestroyWindow(win); SDL_Quit(); debug_log_close(); return 12;
    }
    SDL_RendererInfo info;
    if (SDL_GetRendererInfo(renderer, &info) == 0)
        debug_log_info("PS4 SMOKE: renderer=%s flags=0x%x", info.name, (unsigned) info.flags);
    debug_log_info("PS4 SMOKE: entering visible frame loop");
    unsigned frame = 0; int running = 1;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) running = 0;
            if (event.type == SDL_CONTROLLERBUTTONDOWN && (event.cbutton.button == SDL_CONTROLLER_BUTTON_START || event.cbutton.button == SDL_CONTROLLER_BUTTON_BACK)) running = 0;
        }
        SDL_SetRenderDrawColor(renderer, 18, 62, 130, 255);
        if (SDL_RenderClear(renderer) != 0) debug_log_error("PS4 SMOKE: RenderClear failed: %s", SDL_GetError());
        SDL_Rect bar = { width / 8, height / 3, width * 3 / 4, height / 12 };
        SDL_SetRenderDrawColor(renderer, 235, 190, 45, 255); SDL_RenderFillRect(renderer, &bar);
        SDL_Rect marker = { (int)((frame * 8U) % (unsigned)(width > 64 ? width - 64 : 1)), height / 2, 64, 64 };
        SDL_SetRenderDrawColor(renderer, 80, 220, 120, 255); SDL_RenderFillRect(renderer, &marker);
        SDL_RenderPresent(renderer);
        if (frame == 0) debug_log_info("PS4 SMOKE: first SDL_RenderPresent completed");
        if (frame % 300 == 0) debug_log_info("PS4 SMOKE: frame=%u ticks=%u", frame, SDL_GetTicks());
        ++frame; SDL_Delay(16);
    }
    debug_log_info("PS4 SMOKE: exiting after %u frames", frame);
    SDL_DestroyRenderer(renderer); SDL_DestroyWindow(win); SDL_Quit(); debug_log_close(); return 0;
}
