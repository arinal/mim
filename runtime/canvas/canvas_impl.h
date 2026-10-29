//===-- canvas_impl.h - What the canvas's files share ---------------------===//
//
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// Private to runtime/canvas. Device space is the canvas's pixels; user space
// is what the guest passes, before the transform.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "runtime/canvas/canvas.h"

#include <assert.h>
#include <math.h>

enum {
    // the glyph atlas is cv_atlas_side × cv_atlas_side bytes of coverage
    cv_atlas_side = 1024,
    // glyphs it remembers before it starts over; a power of two, for the hash
    cv_max_glyphs = 4096,
};

// Device coordinates are clamped to ±this, so the arithmetic on them can't overflow; far enough
// off any canvas that nothing on it moves
#define cv_coord_max 1048576.0f

// v clamped to ±cv_coord_max; NaN gives -cv_coord_max
static inline float cv_clamp_coord(const float v) {
    return fminf(fmaxf(v, -cv_coord_max), cv_coord_max);
}

typedef struct cv_point_t {
    float x;
    float y;
} cv_point_t;

typedef struct cv_state_t {
    // device = user × s + t
    float sx;
    float sy;
    float tx;
    float ty;
    uint32_t fill;
    uint32_t stroke;
    float line_width;
    float font_size;
    // the clip, in whole device pixels: [x0, x1) × [y0, y1)
    uint32_t clip_x0;
    uint32_t clip_y0;
    uint32_t clip_x1;
    uint32_t clip_y1;
} cv_state_t;

static inline cv_point_t cv_to_device(const cv_state_t *const s, const float x, const float y) {
    return (cv_point_t){cv_clamp_coord(x * s->sx + s->tx), cv_clamp_coord(y * s->sy + s->ty)};
}

// How much the transform scales lengths that have no direction: line widths, text
static inline float cv_mean_scale(const cv_state_t *const s) {
    return sqrtf(fabsf(s->sx * s->sy));
}

// The first pixel whose centre is at or after `v`, clamped to [0, max]; NaN gives 0
static inline uint32_t cv_pixel_edge(const float v, const uint32_t max) {
    const float e = ceilf(v - 0.5f);
    if (!(e > 0.0f)) {
        return 0;
    }
    return e < (float)max ? (uint32_t)e : max;
}

typedef struct cv_subpath_t {
    uint32_t first;
    uint32_t count;
    bool closed;
} cv_subpath_t;

typedef struct cv_image_t {
    // premultiplied 0xAARRGGBB, rows top to bottom; NULL for a free slot
    uint32_t *pixels;
    uint32_t width;
    uint32_t height;
} cv_image_t;

typedef struct cv_glyph_t {
    // the key: a glyph of the font at a size in quarter pixels; size 0 for an empty slot
    int32_t glyph;
    uint32_t size;
    // where its coverage is in the atlas, and where that goes relative to the pen on the baseline
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
    int16_t left;
    int16_t top;
} cv_glyph_t;

typedef struct cv_impl_t {
    mem_budget_t *budget;
    uint32_t *pixels;
    uint32_t width;
    uint32_t height;

    cv_state_t state;
    cv_state_t saved[cv_max_saves];
    uint32_t depth;

    // the current path, in device space
    cv_point_t *points;
    uint32_t point_count;
    cv_subpath_t *subpaths;
    uint32_t subpath_count;
    // cv_stroke's copy of one subpath, without repeated points
    cv_point_t *scratch;

    // signed area per pixel, (width + 2) × height, for the fill being rasterised; all 0 between fills
    float *cells;
    // the rows and columns that fill touched
    uint32_t dirty_y0;
    uint32_t dirty_y1;
    uint32_t dirty_x0;
    uint32_t dirty_x1;

    cv_image_t images[cv_max_images];

    uint8_t *atlas;
    cv_glyph_t *glyphs;
    uint32_t glyph_count;
    // the shelf glyphs are being packed onto
    uint32_t shelf_x;
    uint32_t shelf_y;
    uint32_t shelf_height;
} cv_impl_t;

// The canvas cv_set_context picked
extern cv_context_t *cv_current;

static inline cv_impl_t *cv_impl(void) {
    assert(cv_current != NULL && "cv_set_context first");
    return cv_current->internal.impl;
}

// raster.c: coverage by signed area, as font-rs and stb_truetype do it

// Adds the line from a to b to the fill being built, any device coordinates
void cv_raster_line(cv_impl_t *const cv, cv_point_t a, cv_point_t b);

// Adds a closed polygon, either way round: overlapping polygons add up
void cv_raster_polygon(cv_impl_t *const cv, const cv_point_t *const points, const uint32_t count);

// Paints what's been added, by the non-zero rule, in `color` (0xAARRGGBB) inside the clip, and
// starts the next fill empty
void cv_raster_paint(cv_impl_t *const cv, const uint32_t color);

// Draws the image scaled so its top left corner is at a and its bottom right at b (device space),
// smoothed, inside the clip
void cv_raster_image(cv_impl_t *const cv, const cv_image_t *const image, const cv_point_t a, const cv_point_t b);

// Paints a coverage mask (0 to 255 a byte, `stride` bytes a row) with its top left at the device
// pixel (x, y), in `color` (0xAARRGGBB), inside the clip
void cv_raster_mask(
    cv_impl_t *const cv,
    const uint8_t *const mask,
    const uint32_t stride,
    const uint32_t width,
    const uint32_t height,
    const int32_t x,
    const int32_t y,
    const uint32_t color
);

// `rgb` (0x00RRGGBB) over the pixel with alpha 0 to 255
void cv_blend(uint32_t *const pixel, const uint32_t rgb, const uint32_t alpha);

// A premultiplied 0xAARRGGBB colour over the pixel
void cv_blend_premultiplied(uint32_t *const pixel, const uint32_t argb);

// image.c

// Decodes a PNG into premultiplied pixels from the budget; NULL if it can't
uint32_t *cv_decode_png(
    mem_budget_t *const budget,
    const uint8_t *const data,
    const size_t size,
    uint32_t *const width,
    uint32_t *const height
);

// text.c

// Sets up the built-in font and an empty atlas
void cv_text_init(cv_impl_t *const cv);
