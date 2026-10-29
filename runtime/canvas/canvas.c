//===-- canvas.c - The software canvas ------------------------------------===//
//
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// Every argument may come from a guest, so nothing here trusts a float: it's
// clamped to the canvas while still a float, and only then made an integer
// (a float outside the integer's range is undefined behaviour in C).
//
//===----------------------------------------------------------------------===//

#include "runtime/canvas/canvas.h"

#include <assert.h>
#include <math.h>
#include <stddef.h>

// ponytail: one canvas at a time, as M5 (cg_set_context). The imports have no context of their own
// to find a second one by.
static cv_context_t *g_cv;

void cv_init(cv_context_t *const ctx, uint32_t *const pixels, const uint32_t width, const uint32_t height) {
    ctx->internal.pixels = pixels;
    ctx->internal.width = width;
    ctx->internal.height = height;
    ctx->internal.fill = 0xFF000000;
}

void cv_set_context(cv_context_t *const ctx) {
    g_cv = ctx;
}

void cv_fill_style(const uint32_t color) {
    assert(g_cv != NULL);
    g_cv->internal.fill = color;
}

// The first pixel whose centre is at or after `v`, clamped to [0, max]; NaN gives 0
static uint32_t edge(const float v, const uint32_t max) {
    const float e = ceilf(v - 0.5f);
    if (!(e > 0.0f)) {
        return 0;
    }
    return e < (float)max ? (uint32_t)e : max;
}

// `src` over `dst`, one 8-bit channel at a time, rounded
static uint32_t blend(const uint32_t dst, const uint32_t src, const uint32_t alpha) {
    uint32_t out = 0;
    for (uint32_t shift = 0; shift < 24; shift += 8) {
        const uint32_t s = (src >> shift) & 0xFF;
        const uint32_t d = (dst >> shift) & 0xFF;
        out |= ((s * alpha + d * (255 - alpha) + 127) / 255) << shift;
    }
    return out;
}

void cv_fill_rect(const float x, const float y, const float width, const float height) {
    assert(g_cv != NULL);
    const cv_context_t *const cv = g_cv;
    // a negative size gives end < start and draws nothing, as in M5 (HTML would flip the rectangle)
    const uint32_t x0 = edge(x, cv->internal.width);
    const uint32_t x1 = edge(x + width, cv->internal.width);
    const uint32_t y0 = edge(y, cv->internal.height);
    const uint32_t y1 = edge(y + height, cv->internal.height);
    const uint32_t color = cv->internal.fill & 0x00FFFFFF;
    const uint32_t alpha = cv->internal.fill >> 24;
    if (alpha == 0) {
        return;
    }
    for (uint32_t py = y0; py < y1; py++) {
        uint32_t *const row = cv->internal.pixels + (size_t)py * cv->internal.width;
        for (uint32_t px = x0; px < x1; px++) {
            row[px] = alpha == 0xFF ? color : blend(row[px], color, alpha);
        }
    }
}
