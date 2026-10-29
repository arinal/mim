//===-- canvas.h - 2D drawing into a pixel buffer, shaped like HTML5 ------===//
//
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// A software canvas. The drawing calls take no context: they draw on the
// one cv_set_context picked, so the guest's mim.cv_* imports can forward
// to them without the guest knowing where the pixels are.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// A canvas and its drawing state; the caller owns the storage, the fields are private to canvas.c
typedef struct cv_context_t {
    struct {
        // width × height pixels of 0x00RRGGBB, rows top to bottom
        uint32_t *pixels;
        uint32_t width;
        uint32_t height;
        // 0xAARRGGBB
        uint32_t fill;
    } internal;
} cv_context_t;

/// Sets up a canvas on `pixels`, which the caller owns and keeps valid while it's in use
///
/// The pixels are left as they are; the fill starts opaque black.
void cv_init(cv_context_t *const ctx, uint32_t *const pixels, const uint32_t width, const uint32_t height);

/// Picks the canvas every other cv_* call draws on; NULL for none
void cv_set_context(cv_context_t *const ctx);

/// Sets the colour of the fills that follow: 0xAARRGGBB, alpha 0xFF covers, less blends
void cv_fill_style(const uint32_t color);

/// Fills every pixel whose centre is inside the rectangle, with the left and top edges inside
///
/// Any floats are safe: a negative size or a NaN draws nothing.
void cv_fill_rect(const float x, const float y, const float width, const float height);

#ifdef __cplusplus
}
#endif
