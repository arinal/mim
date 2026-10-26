//===-- main.c - mim <app.wasm>: runs a guest for a few ticks -------------===//
//
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// The engine's budget table and the main loop: one app_tick per frame, on a
// 60 Hz schedule, until the guest quits, the user presses Ctrl-C, or the
// guest traps.
//
//===----------------------------------------------------------------------===//

#include "runtime/guest/guest.h"
#include "runtime/loop/loop.h"

enum {
    budget_vm,
    budget_linear,
    budget_files,
    budget_count,
};

static const mem_budget_def_t budget_defs[budget_count] = {
    [budget_vm] = {"guest vm", 8 * 1024 * 1024, mem_kind_heap},
    [budget_linear] = {"guest linear", 32 * 1024 * 1024, mem_kind_bump},
    [budget_files] = {"files", 4 * 1024 * 1024, mem_kind_heap},
};

// Reads the whole file at `path` into `budget`; logs and returns NULL on failure
static void * read_file(mem_budget_t * const budget, const char * const path, size_t * const out_size) {
    size_t size;
    if (pl_file_size(path, &size) != pl_result_ok) {
        pl_log(pl_log_severity_error, "mim: can't read '%s'", path);
        return NULL;
    }
    void * const data = mem_try_alloc(budget, size, "wasm file");
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
static bool has_quit(const pl_event_t * const events, const size_t count) {
    for (size_t i = 0; i < count; i++) {
        if (events[i].type == pl_event_quit) {
            return true;
        }
    }
    return false;
}

// Runs frames until the guest quits or dies, or the user asks to stop; returns false if the guest died
static bool run(guest_t * const guest) {
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
        pl_sleep_until_ns(loop_deadline(&loop, pl_time_now_ns()));
    }
    return status != guest_dead;
}

int main(int argc, char ** argv) {
    if (argc != 2) {
        pl_log(pl_log_severity_error, "usage: mim <app.wasm>");
        return 2;
    }
    mem_t mem;
    mem_budget_t budgets[budget_count];
    if (mem_init(&mem, budget_defs, budgets, budget_count) != pl_result_ok) {
        pl_log(pl_log_severity_error, "mim: can't map memory for the budgets");
        return 1;
    }

    bool ok = false;
    size_t size;
    void * const wasm = read_file(&budgets[budget_files], argv[1], &size);
    if (wasm != NULL) {
        guest_t guest;
        ok = guest_load(&guest, &budgets[budget_vm], &budgets[budget_linear], wasm, size)
             && run(&guest);
        guest_unload(&guest);
        mem_free(&budgets[budget_files], wasm);
    }
    const size_t leaks = mem_shutdown(&mem);
    return (ok && leaks == 0) ? 0 : 1;
}
