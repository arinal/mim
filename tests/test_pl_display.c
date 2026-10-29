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

// Pushes a key event as SDL reports one
static void push_key(const SDL_Keycode key, const bool down, const bool repeat) {
    SDL_Event event;
    SDL_zero(event);
    event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    event.key.key = key;
    event.key.down = down;
    event.key.repeat = repeat;
    CHECK(SDL_PushEvent(&event));
}

// Remote keys come through in order with the time last; repeats, other keys and any past the
// buffer don't
static void keys(void) {
    pl_display_t display;
    CHECK(pl_display_open(&display, "test", width, height) == pl_result_ok);
    push_key(SDLK_DOWN, true, false);
    push_key(SDLK_DOWN, true, true);
    push_key(SDLK_A, true, false);
    push_key(SDLK_DOWN, false, false);
    push_key(SDLK_RETURN, true, false);
    push_key(SDLK_ESCAPE, true, false);
    pl_event_t events[8];
    CHECK(pl_tick(events, 8) == 5);
    CHECK(events[0].type == pl_event_key && events[0].key == pl_key_down && events[0].down);
    CHECK(events[1].type == pl_event_key && events[1].key == pl_key_down && !events[1].down);
    CHECK(events[2].type == pl_event_key && events[2].key == pl_key_ok && events[2].down);
    CHECK(events[3].type == pl_event_key && events[3].key == pl_key_back && events[3].down);
    CHECK(events[4].type == pl_event_time && events[4].time_ns == events[0].time_ns);

    // room for one key: the second is dropped, and a quit still fits
    push_key(SDLK_LEFT, true, false);
    push_key(SDLK_RIGHT, true, false);
    SDL_Event quit;
    SDL_zero(quit);
    quit.type = SDL_EVENT_QUIT;
    CHECK(SDL_PushEvent(&quit));
    CHECK(pl_tick(events, 3) == 3);
    CHECK(events[0].type == pl_event_key && events[0].key == pl_key_left);
    CHECK(events[1].type == pl_event_quit && events[2].type == pl_event_time);
    pl_display_close(&display);
}

int main(void) {
    open_present_quit();
    // closing leaves nothing behind: pl_tick doesn't poll a closed window, and it opens again
    pl_event_t events[2];
    CHECK(pl_tick(events, 2) == 1);
    open_present_quit();
    keys();
    return 0;
}
