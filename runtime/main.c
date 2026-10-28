//===-- main.c - mim [--headless] <app.wasm>: runs a guest ----------------===//
//
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// The engine's budget table and the main loop: one app_tick per frame, on a
// 60 Hz schedule, until the guest quits, the user presses Ctrl-C or closes
// the window, or the guest traps. The guest's frames go to a window, unless
// there's no display or --headless.
//
//===----------------------------------------------------------------------===//

#include "runtime/guest/guest.h"
#include "runtime/loop/loop.h"

#include <string.h>

enum {
    budget_vm,
    budget_linear,
    budget_files,
    budget_framebuffer,
    budget_count,
};

static const mem_budget_def_t budget_defs[budget_count] = {
    [budget_vm] = {"guest vm", 8 * 1024 * 1024, mem_kind_heap},
    [budget_linear] = {"guest linear", 32 * 1024 * 1024, mem_kind_bump},
    [budget_files] = {"files", 4 * 1024 * 1024, mem_kind_heap},
    [budget_framebuffer] = {"framebuffer", 4 * 1024 * 1024, mem_kind_bump},
};

// Reads the whole file at `path` into `budget`; logs and returns NULL on failure
static void *read_file(mem_budget_t *const budget, const char *const path, size_t *const out_size) {
    size_t size;
    if (pl_file_size(path, &size) != pl_result_ok) {
        pl_log(pl_log_severity_error, "mim: can't read '%s'", path);
        return NULL;
    }
    void *const data = mem_try_alloc(budget, size, "wasm file");
    if (data == NULL) {
        pl_log(pl_log_severity_error, "mim: '%s' (%zu bytes) doesn't fit in budget 'files'", path, size);
        return NULL;
    }
    if (pl_file_read(path, data, size, out_size) != pl_result_ok) {
        pl_log(pl_log_severity_error, "mim: can't read '%s'", path);
        mem_free(budget, data);
        return NULL;
    }
    return data;
}

// Returns true if the user asked to stop
static bool has_quit(const pl_event_t *const events, const size_t count) {
    for (size_t i = 0; i < count; i++) {
        if (events[i].type == pl_event_quit) {
            return true;
        }
    }
    return false;
}

// Runs frames until the guest quits or dies, or the user asks to stop; returns false if the guest died.
// Shows each frame on `display`, unless it's NULL.
static bool run(guest_t *const guest, pl_display_t *const display, const uint32_t *const framebuffer) {
    loop_t loop = {0};
    pl_event_t events[8];
    guest_status_e status = guest_running;
    while (status == guest_running) {
        const size_t count = pl_tick(events, 8);
        if (has_quit(events, count)) {
            break;
        }
        const loop_frame_t frame = loop_begin(&loop, events[count - 1].time_ns);
        status = guest_tick(guest, frame.abs_time_ms, frame.dt_seconds);
        if (display != NULL) {
            pl_display_present(display, framebuffer);
        }
        pl_sleep_until_ns(loop_deadline(&loop, pl_time_now_ns()));
    }
    return status != guest_dead;
}

int main(int argc, char **argv) {
    const bool headless = argc == 3 && strcmp(argv[1], "--headless") == 0;
    if (argc != 2 && !headless) {
        pl_log(pl_log_severity_error, "usage: mim [--headless] <app.wasm>");
        return 2;
    }
    const char *const path = argv[argc - 1];
    mem_t mem;
    mem_budget_t budgets[budget_count];
    if (mem_init(&mem, budget_defs, budgets, budget_count) != pl_result_ok) {
        pl_log(pl_log_severity_error, "mim: can't map memory for the budgets");
        return 1;
    }

    bool ok = false;
    size_t size;
    void *const wasm = read_file(&budgets[budget_files], path, &size);
    if (wasm != NULL) {
        // black until the guest presents a frame
        const size_t fb_size = (size_t)guest_screen_width * guest_screen_height * 4;
        uint32_t *const framebuffer = mem_alloc(&budgets[budget_framebuffer], fb_size, "framebuffer");
        memset(framebuffer, 0, fb_size);

        pl_display_t storage;
        pl_display_t *display = NULL;
        if (!headless) {
            if (pl_display_open(&storage, "mim", guest_screen_width, guest_screen_height) == pl_result_ok) {
                display = &storage;
            } else {
                pl_log(pl_log_severity_warn, "mim: running without a window");
            }
        }

        guest_t guest;
        ok = guest_load(&guest, &budgets[budget_vm], &budgets[budget_linear], framebuffer, wasm, size)
             && run(&guest, display, framebuffer);
        guest_unload(&guest);
        if (display != NULL) {
            pl_display_close(display);
        }
        mem_reset(&budgets[budget_framebuffer]);
        mem_free(&budgets[budget_files], wasm);
    }
    const size_t leaks = mem_shutdown(&mem);
    return (ok && leaks == 0) ? 0 : 1;
}
