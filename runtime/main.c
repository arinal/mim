//===-- main.c - mim <app.wasm>: runs a guest for a few ticks -------------===//
//
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// The engine's budget table lives here. The real main loop (vsync, dt
// checks) comes later; this runs app_init, three ticks and app_shutdown.
//
//===----------------------------------------------------------------------===//

#include "runtime/guest/guest.h"

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
        ok = guest_load(&guest, &budgets[budget_vm], &budgets[budget_linear], wasm, size);
        const uint64_t start = pl_time_now_ns();
        uint64_t last = start;
        for (int i = 0; ok && i < 3; i++) {
            pl_sleep_ms(16);
            const uint64_t now = pl_time_now_ns();
            ok = guest_tick(&guest, (now - start) / 1000000, (float)(now - last) / 1e9f);
            last = now;
        }
        guest_unload(&guest);
        mem_free(&budgets[budget_files], wasm);
    }
    const size_t leaks = mem_shutdown(&mem);
    return (ok && leaks == 0) ? 0 : 1;
}
