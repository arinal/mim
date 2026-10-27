// SPDX-License-Identifier: MIT OR GPL-2.0-only

// Runs with SDL_VIDEO_DRIVER=dummy (tests/CMakeLists.txt): no display or GPU needed

#include "mim_test.h"
#include "platform/pl.h"

#include <SDL3/SDL.h>

enum {
    width = 64,
    height = 32,
};

static uint32_t pixels[width * height];

// Opens a window, shows two frames, and closes it from a quit event
static void open_present_quit(void) {
    pl_display_t display;
    CHECK(pl_display_open(&display, "test", width, height) == pl_result_ok);
    pl_event_t events[4];
    // nothing happened yet: only the time
    CHECK(pl_tick(events, 4) == 1 && events[0].type == pl_event_time);

    for (size_t i = 0; i < width * height; i++) {
        pixels[i] = (uint32_t)i * 0x010203u;
    }
    pl_display_present(&display, pixels);
    pixels[0] = 0x00FFFFFF;
    pl_display_present(&display, pixels);

    // the window's close button, as SDL reports it: a quit, then the time
    SDL_Event quit;
    SDL_zero(quit);
    quit.type = SDL_EVENT_QUIT;
    CHECK(SDL_PushEvent(&quit));
    CHECK(pl_tick(events, 4) == 2);
    CHECK(events[0].type == pl_event_quit && events[1].type == pl_event_time);
    // drained: the next tick is quiet again
    CHECK(pl_tick(events, 4) == 1 && events[0].type == pl_event_time);
    pl_display_close(&display);
}

int main(void) {
    open_present_quit();
    // closing leaves nothing behind: pl_tick doesn't poll a closed window, and it opens again
    pl_event_t events[2];
    CHECK(pl_tick(events, 2) == 1);
    open_present_quit();
    return 0;
}
