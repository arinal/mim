// SPDX-License-Identifier: MIT OR GPL-2.0-only

#define _GNU_SOURCE // dup, dup2, fileno

#include "mim_test.h"
#include "runtime/canvas/canvas.h"
#include "runtime/guest/guest.h"
#include "runtime/loop/loop.h"

#include <string.h>
#include <unistd.h>

static mem_t mem;
static mem_budget_t budgets[2];
static mem_budget_t *const vm = &budgets[0];
static mem_budget_t *const linear = &budgets[1];

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
static guest_status_e statuses[8];

// Loads `wasm`, ticks it once per frame, unloads it, and returns the "app:" and "guest:" lines
// it logged, without timestamps. Checks both budgets are empty afterwards.
static char *run(
    const void *const wasm,
    const size_t size,
    bool *const loaded,
    const loop_frame_t *const frames,
    const size_t count
) {
    CHECK(count <= sizeof(statuses) / sizeof(statuses[0]));
    // stderr goes to a file while the guest runs; it stays behind, so a sanitizer report
    // from a crash in here can still be read
    FILE *const log = fopen("test_guest.log", "w+");
    CHECK(log != NULL);
    fflush(stderr);
    const int saved = dup(STDERR_FILENO);
    CHECK(saved >= 0 && dup2(fileno(log), STDERR_FILENO) == STDERR_FILENO);

    guest_t guest;
    *loaded = guest_load(&guest, vm, linear, wasm, size);
    for (size_t i = 0; i < count; i++) {
        statuses[i] = guest_tick(&guest, frames[i].abs_time_ms, frames[i].dt_seconds);
    }
    guest_unload(&guest);

    fflush(stderr);
    CHECK(dup2(saved, STDERR_FILENO) == STDERR_FILENO);
    close(saved);

    // nothing left behind: no wasm3 leaks, linear memory handed back
    CHECK(mem_get_stats(vm).used_blocks == 0);
    mem_check(vm);
    CHECK(mem_get_stats(linear).used == 0);

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
    char *const out = run(wasm, size, &loaded, four_frames, 4);
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
        char *const out = run(wasm, size, &loaded, four_frames, 4);
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

    // not a module at all, and a real one cut short
    unsigned char noise[256];
    for (size_t i = 0; i < sizeof(noise); i++) {
        noise[i] = (unsigned char)(i * 131 + 7);
    }
    bool loaded;
    char *out = run(noise, sizeof(noise), &loaded, four_frames, 4);
    CHECK(!loaded && (strstr)(out, "guest: rejected: ") != NULL);
    free(out);

    size_t size;
    void *const wasm = read_guest("hello_rust", &size);
    out = run(wasm, size / 2, &loaded, four_frames, 4);
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
        char *const out = run(wasm, size, &loaded, frames, 3);
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
        free(run(wasm, size, &loaded, &frame, 1));
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
    };
    CHECK(mem_init(&mem, defs, budgets, 2) == pl_result_ok);
    cv_init(&canvas, framebuffer, guest_screen_width, guest_screen_height);
    cv_set_context(&canvas);

    test_same_output();
    test_hostile();
    test_fake_clock();
    test_tick_trap();
    test_bounce();
    test_greedy_twice();
    // and a normal guest still runs after all of that
    test_same_output();

    CHECK(mem_shutdown(&mem) == 0);
    return 0;
}
