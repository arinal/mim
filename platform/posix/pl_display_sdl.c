//===-- pl_display_sdl.c - pl_display_* on SDL3 ---------------------------===//
//
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// A streaming texture the frame is copied into each present, scaled to the
// window with black bars. Pacing is the caller's: vsync is off.
//
//===----------------------------------------------------------------------===//

#include "platform/pl.h"

#include <SDL3/SDL.h>

// C99 has no static_assert: an array with negative size is a compile error
#define PL_STATIC_ASSERT(_name, _cond) typedef char pl_static_assert_##_name[(_cond) ? 1 : -1]

// What actually lives inside the caller's pl_display_t bytes
typedef struct display_context_t {
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    uint32_t width;
} display_context_t;

PL_STATIC_ASSERT(display_fits, sizeof(display_context_t) <= sizeof(pl_display_t));

// defined in pl_posix.c; pl_tick calls it while a window is open
extern bool (*pl_posix_display_poll)(void);

static bool g_open;

static bool poll_events(void) {
    bool quit = false;
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        // with one window, closing it also sends SDL_EVENT_QUIT; either one is enough
        if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
            quit = true;
        }
    }
    return quit;
}

pl_result_e pl_display_open(pl_display_t *const display, const char *const title, const uint32_t width, const uint32_t height) {
    assert(!g_open && "one display at a time");
    assert(width > 0 && height > 0 && width <= INT32_MAX / 4 && height <= INT32_MAX);
    display_context_t *const ctx = (display_context_t *)display;
    SDL_zerop(ctx);

    // pl_tick owns Ctrl-C: SDL's handlers would turn the second one into an event instead of a kill
    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        pl_log(pl_log_severity_warn, "pl: no display: %s", SDL_GetError());
        return pl_result_not_supported;
    }
    const int w = (int)width;
    const int h = (int)height;
    if (!SDL_CreateWindowAndRenderer(title, w, h, SDL_WINDOW_RESIZABLE, &ctx->window, &ctx->renderer)
        || !SDL_SetRenderVSync(ctx->renderer, 0)
        || !SDL_SetRenderLogicalPresentation(ctx->renderer, w, h, SDL_LOGICAL_PRESENTATION_LETTERBOX)
        || (ctx->texture = SDL_CreateTexture(ctx->renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, w, h)) == NULL) {
        pl_log(pl_log_severity_error, "pl: can't open a window: %s", SDL_GetError());
        // each destroy takes NULL
        SDL_DestroyRenderer(ctx->renderer);
        SDL_DestroyWindow(ctx->window);
        SDL_Quit();
        return pl_result_io_error;
    }
    ctx->width = width;
    pl_posix_display_poll = poll_events;
    g_open = true;
    return pl_result_ok;
}

void pl_display_present(pl_display_t *const display, const uint32_t *const pixels) {
    assert(g_open);
    display_context_t *const ctx = (display_context_t *)display;
    // failures here (a lost GPU, a window being torn down) cost one frame; nothing to recover
    SDL_UpdateTexture(ctx->texture, NULL, pixels, (int)(ctx->width * 4));
    SDL_RenderClear(ctx->renderer);
    SDL_RenderTexture(ctx->renderer, ctx->texture, NULL, NULL);
    SDL_RenderPresent(ctx->renderer);
}

void pl_display_close(pl_display_t *const display) {
    assert(g_open);
    display_context_t *const ctx = (display_context_t *)display;
    pl_posix_display_poll = NULL;
    SDL_DestroyTexture(ctx->texture);
    SDL_DestroyRenderer(ctx->renderer);
    SDL_DestroyWindow(ctx->window);
    SDL_Quit();
    SDL_zerop(ctx);
    g_open = false;
}
