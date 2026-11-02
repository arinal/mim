// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// Fuzzes cv_image_load, the one place a guest's bytes reach a third-party parser (stb_image).
// With MIM_FUZZ this is a libFuzzer target; otherwise main() mutates a few valid PNGs with a
// fixed-seed generator, so every build runs the same harness.
//
// Each input is decoded on a fresh canvas and, if it decodes, drawn. Afterwards the canvas's
// budget must be empty: a bad PNG may fail, but it may not leak or write past what it allocated
// (ASan, and mem.c's guard bytes).

#include "mim_test.h"
#include "runtime/canvas/canvas.h"

#include <string.h>

#ifndef MIM_FUZZ
// seeds for main(); static, so it can't clash with the canvas's stb_image
#pragma GCC diagnostic ignored "-Wunused-function"
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include "stb_image_write.h"
#endif

enum {
    side = 16,
};

static mem_t mem;
static mem_budget_t budget;
static uint32_t pixels[side * side];

int LLVMFuzzerTestOneInput(const unsigned char *const data, const size_t size);

int LLVMFuzzerTestOneInput(const unsigned char *const data, const size_t size) {
    // One region for the whole run; small, so a PNG claiming to be huge runs out of budget
    if (mem.base == NULL) {
        static const mem_budget_def_t def = {"fuzz canvas", 8 * 1024 * 1024, mem_kind_heap};
        CHECK(mem_init(&mem, &def, &budget, 1) == pl_result_ok);
    }
    cv_context_t ctx;
    cv_init(&ctx, pixels, side, side, &budget);
    cv_set_context(&ctx);
    const uint32_t image = cv_image_load(data, size);
    if (image != 0) {
        CHECK(cv_draw_image(image, -3, -2, side + 7, side + 5));
    }
    cv_shutdown(&ctx);
    CHECK(mem_get_stats(&budget).used_blocks == 0);
    // mem_free checks each block's guard bytes; this also checks nothing wrote to freed memory,
    // but reads the whole budget, so not every time
    static unsigned runs;
    if (++runs % 64 == 0) {
        mem_check(&budget);
    }
    return 0;
}

#ifndef MIM_FUZZ
static unsigned x = 12345; // xorshift32

static unsigned next(void) {
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return x;
}

int main(void) {
    // valid PNGs of each channel count, as the seeds
    unsigned char raw[7 * 5 * 4];
    for (size_t i = 0; i < sizeof(raw); i++) {
        raw[i] = (unsigned char)(i * 37);
    }
    static unsigned char input[4096];
    for (int channels = 1; channels <= 4; channels++) {
        int size;
        unsigned char *const seed = stbi_write_png_to_mem(raw, 7 * channels, 7, 5, channels, &size);
        CHECK(seed != NULL && (size_t)size <= sizeof(input));
        // the seed itself must decode
        LLVMFuzzerTestOneInput(seed, (size_t)size);
        for (int run = 0; run < 400; run++) {
            memcpy(input, seed, (size_t)size);
            size_t n = (size_t)size;
            // a few bytes changed, some runs cut short; stb_image skips the CRCs, so these reach
            // the decoder
            const unsigned flips = 1 + next() % 8;
            for (unsigned f = 0; f < flips; f++) {
                input[next() % n] = (unsigned char)next();
            }
            if (next() % 4 == 0) {
                n = next() % n;
            }
            LLVMFuzzerTestOneInput(input, n);
        }
        STBIW_FREE(seed);
    }
    mem_check(&budget);
    CHECK(mem_shutdown(&mem) == 0);
    return 0;
}
#endif
