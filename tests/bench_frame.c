// SPDX-License-Identifier: MIT OR GPL-2.0-only

// How long one frame of a guest takes: `bench_frame <app.wasm> [frames]`. The whole of app_tick,
// wasm3 and the canvas included; the window and the 60 Hz wait aren't. Not a test: it prints the
// first frame (which rasterises every glyph into the atlas), then the mean and slowest of the rest.

#include "mim_test.h"
#include "runtime/canvas/canvas.h"
#include "runtime/guest/guest.h"

static uint32_t framebuffer[guest_screen_width * guest_screen_height];

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: bench_frame <app.wasm> [frames]\n");
        return 2;
    }
    const int frames = argc > 2 ? atoi(argv[2]) : 300;
    CHECK(frames >= 2);
    static const mem_budget_def_t defs[] = {
        {"guest vm", 8 * 1024 * 1024, mem_kind_heap},
        {"guest linear", 32 * 1024 * 1024, mem_kind_bump},
        {"canvas", 32 * 1024 * 1024, mem_kind_heap},
    };
    mem_t mem;
    mem_budget_t budgets[3];
    CHECK(mem_init(&mem, defs, budgets, 3) == pl_result_ok);
    size_t size;
    CHECK(pl_file_size(argv[1], &size) == pl_result_ok);
    void *const wasm = malloc(size);
    CHECK(wasm != NULL && pl_file_read(argv[1], wasm, size, &size) == pl_result_ok);

    cv_context_t canvas;
    cv_init(&canvas, framebuffer, guest_screen_width, guest_screen_height, &budgets[2]);
    cv_set_context(&canvas);
    guest_t guest;
    CHECK(guest_load(&guest, &budgets[0], &budgets[1], wasm, size));
    uint64_t first = 0;
    uint64_t total = 0;
    uint64_t worst = 0;
    for (int i = 0; i < frames; i++) {
        const uint64_t start = pl_time_now_ns();
        CHECK(guest_tick(&guest, (uint64_t)i * 16, 0.016f) == guest_running);
        const uint64_t ns = pl_time_now_ns() - start;
        if (i == 0) {
            first = ns;
            continue;
        }
        total += ns;
        worst = ns > worst ? ns : worst;
    }
    printf("%s: first frame %.2f ms; the next %d: mean %.2f ms, slowest %.2f ms\n", argv[1], (double)first / 1e6, frames - 1, (double)total / (frames - 1) / 1e6, (double)worst / 1e6);
    guest_unload(&guest);
    cv_shutdown(&canvas);
    free(wasm);
    CHECK(mem_shutdown(&mem) == 0);
    return 0;
}
