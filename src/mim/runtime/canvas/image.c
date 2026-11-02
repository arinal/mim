//===-- image.c - PNGs from a guest, decoded by stb_image -----------------===//
//
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// The bytes are the guest's, so this is a sandbox wall: only the PNG decoder
// is built, an image over cv_max_image_side is refused before it's
// allocated, and every allocation comes from the canvas budget, so a bad
// PNG can't take more than the budget holds. fuzz_image fuzzes this file.
//
//===----------------------------------------------------------------------===//

#include "runtime/canvas/canvas_impl.h"

#include <assert.h>
#include <limits.h>
#include <string.h>

// ponytail: stb_image's allocation hooks take no context, so the budget is set around each decode
static mem_budget_t *g_budget;

static void *stb_malloc(const size_t size) {
    return mem_try_alloc(g_budget, size, "png");
}

static void stb_free(void *const ptr) {
    if (ptr != NULL) {
        mem_free(g_budget, ptr);
    }
}

// The budgets have no realloc, but stb_image says how big the old block was
static void *stb_realloc(void *const ptr, const size_t old_size, const size_t new_size) {
    void *const q = stb_malloc(new_size);
    if (q != NULL && ptr != NULL) {
        memcpy(q, ptr, old_size < new_size ? old_size : new_size);
        stb_free(ptr);
    }
    return q;
}

// static, so nothing else that links stb_image clashes with this copy
#pragma GCC diagnostic ignored "-Wunused-function"
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_MAX_DIMENSIONS cv_max_image_side
#define STBI_ASSERT(_x) assert(_x)
#define STBI_MALLOC(_size) stb_malloc(_size)
#define STBI_FREE(_ptr) stb_free(_ptr)
#define STBI_REALLOC_SIZED(_ptr, _old, _new) stb_realloc(_ptr, _old, _new)
#include "stb_image.h"

uint32_t *cv_decode_png(
    mem_budget_t *const budget,
    const uint8_t *const data,
    const size_t size,
    uint32_t *const width,
    uint32_t *const height
) {
    if (size > INT_MAX) {
        return NULL;
    }
    g_budget = budget;
    int w = 0;
    int h = 0;
    int channels = 0;
    unsigned char *const rgba = stbi_load_from_memory(data, (int)size, &w, &h, &channels, 4);
    g_budget = NULL;
    if (rgba == NULL) {
        return NULL;
    }
    // RGBA bytes to premultiplied 0xAARRGGBB, in place: each pixel is read before it's written
    uint32_t *const pixels = (uint32_t *)(void *)rgba;
    const size_t count = (size_t)w * (size_t)h;
    for (size_t i = 0; i < count; i++) {
        const uint32_t a = rgba[i * 4 + 3];
        const uint32_t r = (rgba[i * 4 + 0] * a + 127) / 255;
        const uint32_t g = (rgba[i * 4 + 1] * a + 127) / 255;
        const uint32_t b = (rgba[i * 4 + 2] * a + 127) / 255;
        pixels[i] = a << 24 | r << 16 | g << 8 | b;
    }
    *width = (uint32_t)w;
    *height = (uint32_t)h;
    return pixels;
}
