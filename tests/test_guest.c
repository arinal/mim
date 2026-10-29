// SPDX-License-Identifier: MIT OR GPL-2.0-only

#define _GNU_SOURCE // dup, dup2, fileno

#include "golden.h"
#include "mim_test.h"
#include "runtime/canvas/canvas.h"
#include "runtime/guest/guest.h"
#include "runtime/loop/loop.h"

#include <string.h>
#include <unistd.h>

static mem_t mem;
static mem_budget_t budgets[3];
static mem_budget_t *const vm = &budgets[0];
static mem_budget_t *const linear = &budgets[1];
static mem_budget_t *const canvas_budget = &budgets[2];

// what the guests' mim.cv_* calls draw on
static uint32_t framebuffer[guest_screen_width * guest_screen_height];
static cv_context_t canvas;

// Reads build/<preset>/guests/<name>.wasm into a malloc'd buffer
static void *read_guest(const char *const name, size_t *const out_size) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.wasm", MIM_GUEST_DIR, name);
    size_t size;
    CHECK(pl_file_size(path, &size) == pl_result_ok);
    void *const data = malloc(size);
    CHECK(data != NULL);
    CHECK(pl_file_read(path, data, size, out_size) == pl_result_ok);
    return data;
}

// Four frames, 16 ms apart; the hello guests quit on the third
static const loop_frame_t four_frames[] = {{16, 0.016f}, {32, 0.016f}, {48, 0.016f}, {64, 0.016f}};

// What guest_tick returned for each frame of the last run
static guest_status_e statuses[64];

// A scripted key: in frame `frame`, `key` goes down or up
typedef struct key_at_t {
    size_t frame;
    pl_key_e key;
    bool down;
} key_at_t;

// If set, run() calls it after each frame
static void (*after_frame)(size_t frame);

// Loads `wasm`, ticks it once per frame with the `keys` due in it, unloads it, and returns the
// "app:" and "guest:" lines it logged, without timestamps. Checks both budgets are empty afterwards.
static char *run(
    const void *const wasm,
    const size_t size,
    bool *const loaded,
    const loop_frame_t *const frames,
    const size_t count,
    const key_at_t *const keys,
    const size_t key_count
) {
    CHECK(count <= sizeof(statuses) / sizeof(statuses[0]));
    // stderr goes to a file while the guest runs; it stays behind, so a sanitizer report
    // from a crash in here can still be read
    FILE *const log = fopen("test_guest.log", "w+");
    CHECK(log != NULL);
    fflush(stderr);
    const int saved = dup(STDERR_FILENO);
    CHECK(saved >= 0 && dup2(fileno(log), STDERR_FILENO) == STDERR_FILENO);

    // a fresh canvas each time, so nothing a guest loads outlives it
    cv_init(&canvas, framebuffer, guest_screen_width, guest_screen_height, canvas_budget);
    cv_set_context(&canvas);
    guest_t guest;
    *loaded = guest_load(&guest, vm, linear, wasm, size);
    for (size_t i = 0; i < count; i++) {
        // the frame's keys, then its time, as pl_tick hands them over
        pl_event_t events[8];
        size_t n = 0;
        for (size_t k = 0; k < key_count; k++) {
            if (keys[k].frame == i) {
                CHECK(n < 7);
                events[n++] = (pl_event_t){pl_event_key, 0, keys[k].key, keys[k].down};
            }
        }
        events[n++] = (pl_event_t){.type = pl_event_time};
        statuses[i] = guest_tick(&guest, frames[i].abs_time_ms, frames[i].dt_seconds, events, n);
        if (after_frame != NULL) {
            after_frame(i);
        }
    }
    guest_unload(&guest);
    cv_shutdown(&canvas);

    fflush(stderr);
    CHECK(dup2(saved, STDERR_FILENO) == STDERR_FILENO);
    close(saved);

    // nothing left behind: no wasm3 leaks, linear memory handed back
    CHECK(mem_get_stats(vm).used_blocks == 0);
    mem_check(vm);
    CHECK(mem_get_stats(linear).used == 0);
    CHECK(mem_get_stats(canvas_budget).used_blocks == 0);

    // keep "app: ..." and "guest: ...", dropping "<severity> <seconds>.<ms> "
    static char line[1024];
    char *const out = calloc(64 * 1024, 1);
    CHECK(out != NULL);
    rewind(log);
    while (fgets(line, sizeof(line), log) != NULL) {
        const char *start = (strstr)(line, " app: ");
        if (start == NULL) {
            start = (strstr)(line, " guest: ");
        }
        if (start != NULL) {
            CHECK(strlen(out) + strlen(start) < 64 * 1024);
            strcat(out, start + 1);
        }
    }
    fclose(log);
    return out;
}

// Runs the guest called `name` and checks what guest_load returned (false for a trap in app_init too) and what it logged
static void expect(const char *const name, const bool should_load, const char *const expected) {
    size_t size;
    void *const wasm = read_guest(name, &size);
    bool loaded;
    char *const out = run(wasm, size, &loaded, four_frames, 4, NULL, 0);
    if (loaded != should_load || (strstr)(out, expected) == NULL) {
        fprintf(stderr, "%s: expected %s and \"%s\" in:\n%s\n", name, should_load ? "loaded" : "rejected", expected, out);
        CHECK(false);
    }
    // a guest that didn't load is never ticked
    CHECK(loaded || statuses[0] == guest_dead);
    free(out);
    free(wasm);
}

static const char hello_log[] =
    "app: hello\n"
    "app: sum of 0..99999: 4999950000\n"
    "app: tick 1 at 16 ms, dt 16 ms\n"
    "app: tick 2 at 32 ms, dt 16 ms\n"
    "app: tick 3 at 48 ms, dt 16 ms\n"
    "app: bye after 3 ticks\n";

// The C++ and the Rust guest log exactly the same lines, and grow linear memory to do it
static void test_same_output(void) {
    const char *const names[] = {"hello_cpp", "hello_rust"};
    for (size_t i = 0; i < 2; i++) {
        size_t size;
        void *const wasm = read_guest(names[i], &size);
        bool loaded;
        char *const out = run(wasm, size, &loaded, four_frames, 4, NULL, 0);
        if (!loaded || strcmp(out, hello_log) != 0) {
            fprintf(stderr, "%s logged:\n%s\nexpected:\n%s\n", names[i], out, hello_log);
            CHECK(false);
        }
        // quits on the third tick; the fourth doesn't call in (the log has no "tick 4")
        CHECK(statuses[0] == guest_running && statuses[1] == guest_running);
        CHECK(statuses[2] == guest_quit && statuses[3] == guest_quit);
        // and never draw
        for (size_t p = 0; p < guest_screen_width * guest_screen_height; p++) {
            CHECK(framebuffer[p] == 0);
        }
        free(out);
        free(wasm);
    }
    // both start with 17 pages (a 1 MiB stack, then data); the 400 KB array needed more
    CHECK(mem_get_stats(linear).peak > 17 * 65536);
}

static void test_hostile(void) {
    expect("evil_bad_pointer", false, "guest: trapped in app_init: mim.log: text outside guest memory");
    expect("evil_wrapping_pointer", false, "guest: trapped in app_init: mim.log: text outside guest memory");
    expect("evil_log_edge", false, "app: edge\nguest: trapped in app_init: mim.log: text outside guest memory");
    expect("evil_huge_len", false, "guest: trapped in app_init: mim.log: text outside guest memory");
    expect("evil_bad_level", false, "guest: trapped in app_init: mim.log: bad level");
    // one line, not two
    expect("evil_log_injection", true, "app: ok?E 1.000 fake?\napp: tick\n");
    // cut to 200 bytes, and marked
    char long_line[5 + 200 + 5] = "app: ";
    memset(long_line + 5, 'x', 200);
    memcpy(long_line + 205, "...\n", 5);
    expect("evil_log_long", true, long_line);
    expect("evil_recursion", false, "guest: trapped in app_init: [trap] stack overflow");
    expect("evil_unknown_import", false, "guest: trapped in app_init: function body is missing");
    expect("evil_missing_export", false, "guest: rejected: export 'app_tick': function lookup failed");
    expect("evil_wrong_signature", false, "guest: rejected: export 'app_tick' has the wrong signature");
    expect("evil_wrong_types", false, "guest: rejected: export 'app_tick' has the wrong signature");
    expect("evil_wrong_import", false, "guest: rejected: import 'mim.log': function signature mismatch");
    expect("evil_huge_memory", false, "guest: rejected: memory allocation failed");
    expect("evil_imported_memory", false, "guest: rejected: unallocated linear memory");
    expect("evil_cv_save_deep", false, "guest: trapped in app_init: mim.cv_save: more than 32 saves");
    expect("evil_cv_path_full", false, "guest: trapped in app_init: mim.cv_line_to: path over 16384 points or 1024 subpaths");
    expect("evil_cv_bad_image", false, "guest: trapped in app_init: mim.cv_draw_image: no such image");
    expect("evil_cv_negative_arc", false, "guest: trapped in app_init: mim.cv_arc: negative radius");
    expect("evil_cv_bad_png", false, "app: refused\nguest: trapped in app_init: mim.cv_image_load: png outside guest memory");
    expect("evil_read_events_edge", false, "app: edge\nguest: trapped in app_init: mim.read_events: events outside guest memory");
    expect("evil_read_events_huge_cap", false, "guest: trapped in app_init: mim.read_events: events outside guest memory");

    // not a module at all, and a real one cut short
    unsigned char noise[256];
    for (size_t i = 0; i < sizeof(noise); i++) {
        noise[i] = (unsigned char)(i * 131 + 7);
    }
    bool loaded;
    char *out = run(noise, sizeof(noise), &loaded, four_frames, 4, NULL, 0);
    CHECK(!loaded && (strstr)(out, "guest: rejected: ") != NULL);
    free(out);

    size_t size;
    void *const wasm = read_guest("hello_rust", &size);
    out = run(wasm, size / 2, &loaded, four_frames, 4, NULL, 0);
    CHECK(!loaded && (strstr)(out, "guest: rejected: ") != NULL);
    free(out);
    free(wasm);
}

// The roadmap's 4a test: frames from a fake clock go through loop_begin, and both guests see
// the dt rules: 1 ms for the first frame and after a pause over 1 s
static void test_fake_clock(void) {
    static const char expected[] =
        "app: hello\n"
        "app: sum of 0..99999: 4999950000\n"
        "app: tick 1 at 0 ms, dt 1 ms\n"
        "app: tick 2 at 16 ms, dt 17 ms\n"
        "app: tick 3 at 5016 ms, dt 1 ms\n"
        "app: bye after 3 ticks\n";
    const uint64_t t0 = 987654321000;
    const uint64_t times[] = {t0, t0 + loop_frame_ns, t0 + loop_frame_ns + 5000 * 1000000ull};
    loop_t loop = {0};
    loop_frame_t frames[3];
    for (size_t i = 0; i < 3; i++) {
        frames[i] = loop_begin(&loop, times[i]);
    }
    const char *const names[] = {"hello_cpp", "hello_rust"};
    for (size_t i = 0; i < 2; i++) {
        size_t size;
        void *const wasm = read_guest(names[i], &size);
        bool loaded;
        char *const out = run(wasm, size, &loaded, frames, 3, NULL, 0);
        if (!loaded || strcmp(out, expected) != 0) {
            fprintf(stderr, "%s logged:\n%s\nexpected:\n%s\n", names[i], out, expected);
            CHECK(false);
        }
        free(out);
        free(wasm);
    }
}

// A trap in app_tick: the guest is dead from then on, and app_shutdown isn't called
static void test_tick_trap(void) {
    expect("evil_tick_trap", true, "app: init\nguest: trapped in app_tick: [trap] unreachable executed\n");
    CHECK(statuses[0] == guest_dead && statuses[3] == guest_dead);
}

// The pixel at (x, y)
static uint32_t pixel(const size_t x, const size_t y) {
    return framebuffer[y * guest_screen_width + x];
}

// bounce_rust's box is where abs_time_ms says: 64 × 64 white at x = t / 4, rows 328 to 391,
// on dark blue
static void test_bounce(void) {
    const uint32_t white = 0x00FFFFFF;
    const uint32_t blue = 0x00202040;
    const struct {
        uint64_t abs_time_ms;
        size_t x;
    } cases[] = {{0, 0}, {400, 100}, {4864, 1216}, {5264, 1116}};
    size_t size;
    void *const wasm = read_guest("bounce_rust", &size);
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        const loop_frame_t frame = {cases[i].abs_time_ms, 0.016f};
        bool loaded;
        free(run(wasm, size, &loaded, &frame, 1, NULL, 0));
        CHECK(loaded && statuses[0] == guest_running);
        const size_t x = cases[i].x;
        // the four corners inside, and the pixels just outside each edge
        CHECK(pixel(x, 328) == white && pixel(x + 63, 328) == white);
        CHECK(pixel(x, 391) == white && pixel(x + 63, 391) == white);
        CHECK(pixel(x, 327) == blue && pixel(x, 392) == blue);
        CHECK(x == 0 || pixel(x - 1, 328) == blue);
        CHECK(x + 64 == guest_screen_width || pixel(x + 64, 328) == blue);
        CHECK(pixel(0, 0) == blue && pixel(1279, 719) == blue);
    }
    free(wasm);
    memset(framebuffer, 0, sizeof(framebuffer));
}

// The canvas demo, in Rust and in C++: every canvas import through both SDKs, the same frame pixel
// for pixel, and the golden one
static void test_canvas_demo(void) {
    static uint32_t rust_frame[guest_screen_width * guest_screen_height];
    const char *const names[] = {"canvas_rust", "canvas_cpp"};
    const loop_frame_t frame = {1000, 0.016f};
    for (size_t i = 0; i < 2; i++) {
        size_t size;
        void *const wasm = read_guest(names[i], &size);
        bool loaded;
        char *const out = run(wasm, size, &loaded, &frame, 1, NULL, 0);
        if (!loaded || statuses[0] != guest_running) {
            fprintf(stderr, "%s logged:\n%s\n", names[i], out);
            CHECK(false);
        }
        free(out);
        free(wasm);
        if (i == 0) {
            memcpy(rust_frame, framebuffer, sizeof(framebuffer));
        } else if (memcmp(rust_frame, framebuffer, sizeof(framebuffer)) != 0) {
            fprintf(stderr, "canvas_cpp's frame differs from canvas_rust's\n");
            CHECK(golden_check("canvas_demo_cpp", framebuffer, guest_screen_width, guest_screen_height));
            CHECK(false);
        }
    }
    CHECK(golden_check("canvas_demo", framebuffer, guest_screen_width, guest_screen_height));
    memset(framebuffer, 0, sizeof(framebuffer));
}

// The UI guests' frames worth keeping, and which frames they are
static uint32_t ui_home[guest_screen_width * guest_screen_height];
static uint32_t ui_details[guest_screen_width * guest_screen_height];
static uint32_t ui_back[guest_screen_width * guest_screen_height];
enum {
    ui_home_frame = 33,
    ui_details_frame = 37,
    ui_back_frame = 41,
};

static void keep_ui_frames(const size_t frame) {
    if (frame == ui_home_frame) {
        memcpy(ui_home, framebuffer, sizeof(framebuffer));
    } else if (frame == ui_details_frame) {
        memcpy(ui_details, framebuffer, sizeof(framebuffer));
    } else if (frame == ui_back_frame) {
        memcpy(ui_back, framebuffer, sizeof(framebuffer));
    }
}

// Frames 50 ms apart with dt pinned, like a story test: anything animated depends on the frame count only
static loop_frame_t ui_frames[42];

static void make_ui_frames(void) {
    for (size_t i = 0; i < sizeof(ui_frames) / sizeof(ui_frames[0]); i++) {
        ui_frames[i] = (loop_frame_t){(uint64_t)i * 50, 0.05f};
    }
}

// The roadmap's 4c test, a scripted replay: right five times, then down, down, OK, and back.
// Both UI guests draw the same frames, the home screen and the details page match their golden
// images, and Back returns to exactly the home screen it left, focus and scroll included.
static void test_ui_replay(void) {
    static const key_at_t script[] = {
        {1, pl_key_right, true},
        {2, pl_key_right, false},
        {3, pl_key_right, true},
        {4, pl_key_right, false},
        {5, pl_key_right, true},
        {6, pl_key_right, false},
        {7, pl_key_right, true},
        {8, pl_key_right, false},
        {9, pl_key_right, true},
        {10, pl_key_right, false},
        // row 0 has scrolled, row 1 hasn't: the tile under the same spot is the fifth, not the sixth
        {11, pl_key_down, true},
        {12, pl_key_down, false},
        {13, pl_key_down, true},
        {14, pl_key_down, false},
        {34, pl_key_ok, true},
        {35, pl_key_ok, false},
        {38, pl_key_back, true},
        {39, pl_key_back, false},
    };
    static const char expected[] =
        "app: ui: focus 0,1\n"
        "app: ui: focus 0,2\n"
        "app: ui: focus 0,3\n"
        "app: ui: focus 0,4\n"
        "app: ui: focus 0,5\n"
        "app: ui: focus 1,4\n"
        "app: ui: focus 2,4\n"
        "app: ui: details 2,4\n"
        "app: ui: home\n";
    static uint32_t rust_home[guest_screen_width * guest_screen_height];
    static uint32_t rust_details[guest_screen_width * guest_screen_height];
    const size_t frames = sizeof(ui_frames) / sizeof(ui_frames[0]);
    make_ui_frames();
    const char *const names[] = {"ui_rust", "ui_cpp"};
    for (size_t i = 0; i < 2; i++) {
        size_t size;
        void *const wasm = read_guest(names[i], &size);
        bool loaded;
        after_frame = keep_ui_frames;
        char *const out = run(wasm, size, &loaded, ui_frames, frames, script, sizeof(script) / sizeof(script[0]));
        after_frame = NULL;
        if (!loaded || statuses[frames - 1] != guest_running || strcmp(out, expected) != 0) {
            fprintf(stderr, "%s logged:\n%s\nexpected:\n%s\n", names[i], out, expected);
            CHECK(false);
        }
        free(out);
        free(wasm);
        CHECK(memcmp(ui_back, ui_home, sizeof(ui_home)) == 0);
        if (i == 0) {
            memcpy(rust_home, ui_home, sizeof(ui_home));
            memcpy(rust_details, ui_details, sizeof(ui_details));
        } else if (memcmp(rust_home, ui_home, sizeof(ui_home)) != 0 || memcmp(rust_details, ui_details, sizeof(ui_details)) != 0) {
            fprintf(stderr, "ui_cpp's frames differ from ui_rust's\n");
            CHECK(golden_check("ui_home_cpp", ui_home, guest_screen_width, guest_screen_height));
            CHECK(golden_check("ui_details_cpp", ui_details, guest_screen_width, guest_screen_height));
            CHECK(false);
        }
    }
    CHECK(golden_check("ui_home", ui_home, guest_screen_width, guest_screen_height));
    CHECK(golden_check("ui_details", ui_details, guest_screen_width, guest_screen_height));
    memset(framebuffer, 0, sizeof(framebuffer));
}

// Holding a direction moves once, then again after 0.30 s and every 0.15 s, until it's let go;
// Back from the top row quits
static void test_ui_repeat(void) {
    // right held from frame 1 until frame 21: held for 0.05 s more each frame from frame 2
    static const key_at_t script[] = {
        {1, pl_key_right, true},
        {21, pl_key_right, false},
        {30, pl_key_back, true},
    };
    // the press, then repeats at 0.30, 0.45, 0.60, 0.75 and 0.90 s; at 1.0 s the release comes
    // after the time is counted, which is one frame's grace: 1.05 is past it
    static const char expected[] =
        "app: ui: focus 0,1\n"
        "app: ui: focus 0,2\n"
        "app: ui: focus 0,3\n"
        "app: ui: focus 0,4\n"
        "app: ui: focus 0,5\n"
        "app: ui: focus 0,6\n"
        "app: ui: bye\n";
    make_ui_frames();
    const char *const names[] = {"ui_rust", "ui_cpp"};
    for (size_t i = 0; i < 2; i++) {
        size_t size;
        void *const wasm = read_guest(names[i], &size);
        bool loaded;
        char *const out = run(wasm, size, &loaded, ui_frames, 32, script, sizeof(script) / sizeof(script[0]));
        if (!loaded || strcmp(out, expected) != 0) {
            fprintf(stderr, "%s logged:\n%s\nexpected:\n%s\n", names[i], out, expected);
            CHECK(false);
        }
        CHECK(statuses[29] == guest_running && statuses[30] == guest_quit);
        free(out);
        free(wasm);
    }
    memset(framebuffer, 0, sizeof(framebuffer));
}

// A guest takes all of linear memory and dirties it; the next one still gets zeroed pages.
// 8 MiB is 128 pages, minus the 17 it starts with and the block's header.
static void test_greedy_twice(void) {
    expect("evil_greedy", true, "app: grew 0110 pages, all zero\napp: tick\n");
    expect("evil_greedy", true, "app: grew 0110 pages, all zero\napp: tick\n");
    CHECK(mem_get_stats(linear).peak <= mem_get_stats(linear).size);
}

int main(void) {
    const mem_budget_def_t defs[] = {
        {"guest vm", 8 * 1024 * 1024, mem_kind_heap},
        {"guest linear", 8 * 1024 * 1024, mem_kind_bump},
        {"canvas", 32 * 1024 * 1024, mem_kind_heap},
    };
    CHECK(mem_init(&mem, defs, budgets, 3) == pl_result_ok);

    test_same_output();
    test_hostile();
    test_fake_clock();
    test_tick_trap();
    test_bounce();
    test_canvas_demo();
    test_ui_replay();
    test_ui_repeat();
    test_greedy_twice();
    // and a normal guest still runs after all of that
    test_same_output();

    CHECK(mem_shutdown(&mem) == 0);
    return 0;
}
