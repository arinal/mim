//===-- canvas.h - 2D drawing into a pixel buffer, shaped like HTML5 ------===//
//
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// A software canvas. The drawing calls take no context: they draw on the
// one cv_set_context picked, so the guest's mim.cv_* imports can forward
// to them without the guest knowing where the pixels are.
//
// Every argument may come from a guest. A call with a NaN or infinite
// argument does nothing, as in HTML; a call that would need more than the
// limits below returns false and leaves the canvas as it was.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "runtime/mem/mem.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    /// Points in the current path, after curves are cut into lines
    cv_max_points = 16384,
    /// Subpaths in the current path: each cv_move_to and cv_rounded_rect starts one
    cv_max_subpaths = 1024,
    /// How deep cv_save can go
    cv_max_saves = 32,
    /// Images loaded at once
    cv_max_images = 64,
    /// The widest and tallest image cv_image_load decodes
    cv_max_image_side = 4096,
};

/// A canvas; the caller owns the storage, the fields are private to the canvas
typedef struct cv_context_t {
    struct {
        struct cv_impl_t *impl;
    } internal;
} cv_context_t;

/// Sets up a canvas on `pixels`: width × height of 0x00RRGGBB, rows top to bottom
///
/// The caller owns `pixels` and keeps them valid until `cv_shutdown`; they're left as they are.
/// Everything else the canvas needs (about 5 MiB at 1280×720, plus the images it loads) comes from
/// the heap budget `budget`, and too small a budget aborts here.
void cv_init(
    cv_context_t *const ctx,
    uint32_t *const pixels,
    const uint32_t width,
    const uint32_t height,
    mem_budget_t *const budget
);

/// Frees everything the canvas took from its budget, images included
void cv_shutdown(cv_context_t *const ctx);

/// Picks the canvas every other cv_* call draws on; NULL for none
void cv_set_context(cv_context_t *const ctx);

//===----------------------------------------------------------------------===//
// State: what cv_save keeps and cv_restore brings back
//
// It starts as: no transform, opaque black fill and stroke, line width 1,
// font size 10, and the whole canvas as the clip.
//===----------------------------------------------------------------------===//

/// Pushes the state; false if it's already cv_max_saves deep
bool cv_save(void);

/// Pops the state; does nothing if nothing was saved
void cv_restore(void);

/// Moves what's drawn from now on by (x, y), in the current transform's units
void cv_translate(const float x, const float y);

/// Scales what's drawn from now on; text and line widths scale by sqrt(|x × y|)
void cv_scale(const float x, const float y);

/// Sets the colour of the fills that follow: 0xAARRGGBB, alpha 0xFF covers, less blends
void cv_fill_style(const uint32_t color);

/// Sets the colour of the strokes that follow, as for cv_fill_style
void cv_stroke_style(const uint32_t color);

/// Sets the stroke width; a width of 0 or less is ignored, as in HTML
void cv_line_width(const float width);

/// Sets the text size: the font's em square, in the current transform's units; 0 or less is ignored
void cv_font_size(const float size);

/// Narrows the clip to the rectangle, rounded to whole pixels by their centres; only cv_restore widens it
void cv_clip_rect(const float x, const float y, const float width, const float height);

//===----------------------------------------------------------------------===//
// Paths
//===----------------------------------------------------------------------===//

/// Empties the current path
void cv_begin_path(void);

/// Starts a subpath at (x, y); false if the path is full
bool cv_move_to(const float x, const float y);

/// Adds a line from the current point; with no current point it's a cv_move_to. False if the path is full.
bool cv_line_to(const float x, const float y);

/// Adds an arc of the circle at (x, y), from angle `start` to `end` in radians, clockwise on
/// screen unless `counterclockwise`. A line joins it to the current point. `radius` must be 0 or
/// more. False if the path is full.
bool cv_arc(
    const float x,
    const float y,
    const float radius,
    const float start,
    const float end,
    const bool counterclockwise
);

/// Adds the rectangle as a closed subpath, with corners rounded by `radius` (at most half the
/// shorter side). False if the path is full.
bool cv_rounded_rect(
    const float x,
    const float y,
    const float width,
    const float height,
    const float radius
);

/// Closes the subpath with a line back to its start
void cv_close_path(void);

/// Fills the path with the fill style, by the non-zero winding rule; every subpath counts as closed
void cv_fill(void);

/// Strokes the path with the stroke style and line width: butt ends, mitred corners (bevelled past a
/// miter limit of 10)
void cv_stroke(void);

/// Fills a rectangle without touching the current path
void cv_fill_rect(const float x, const float y, const float width, const float height);

//===----------------------------------------------------------------------===//
// Images
//===----------------------------------------------------------------------===//

/// Decodes a PNG and keeps it; returns a handle for cv_draw_image, or 0 if it isn't a PNG this can
/// decode, is bigger than cv_max_image_side, or there's no room for it. The bytes can go afterwards.
uint32_t cv_image_load(const uint8_t *const data, const size_t size);

/// Draws an image scaled into the rectangle, smoothed; false if `image` isn't a loaded image
bool cv_draw_image(
    const uint32_t image,
    const float x,
    const float y,
    const float width,
    const float height
);

//===----------------------------------------------------------------------===//
// Text, in the one font built in (Liberation Sans), Latin only
//===----------------------------------------------------------------------===//

/// Draws UTF-8 text with the fill style, starting at (x, y) on its baseline. Bytes that aren't
/// UTF-8 draw as U+FFFD.
void cv_fill_text(const uint8_t *const text, const size_t size, const float x, const float y);

/// How wide cv_fill_text would draw the text, in the current transform's units
float cv_measure_text(const uint8_t *const text, const size_t size);

#ifdef __cplusplus
}
#endif
