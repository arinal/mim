//===-- host_cv.c - The mim.cv_* imports: the guest's canvas --------------===//
//
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// Each forwards to the canvas cv_set_context picked, which already handles
// any float a guest sends. What's left here is what traps: a limit hit, a
// negative radius, an image that isn't one.
//
//===----------------------------------------------------------------------===//

#include "runtime/canvas/canvas.h"
#include "runtime/guest/ffi.h"

const char *host_cv_save(const guest_mem_t *const mem) {
    (void)mem;
    return cv_save() ? NULL : "mim.cv_save: more than 32 saves";
}

const char *host_cv_restore(const guest_mem_t *const mem) {
    (void)mem;
    cv_restore();
    return NULL;
}

const char *host_cv_translate(const guest_mem_t *const mem, const float x, const float y) {
    (void)mem;
    cv_translate(x, y);
    return NULL;
}

const char *host_cv_scale(const guest_mem_t *const mem, const float x, const float y) {
    (void)mem;
    cv_scale(x, y);
    return NULL;
}

const char *host_cv_fill_style(const guest_mem_t *const mem, const uint32_t color) {
    (void)mem;
    cv_fill_style(color);
    return NULL;
}

const char *host_cv_stroke_style(const guest_mem_t *const mem, const uint32_t color) {
    (void)mem;
    cv_stroke_style(color);
    return NULL;
}

const char *host_cv_line_width(const guest_mem_t *const mem, const float width) {
    (void)mem;
    cv_line_width(width);
    return NULL;
}

const char *host_cv_font_size(const guest_mem_t *const mem, const float size) {
    (void)mem;
    cv_font_size(size);
    return NULL;
}

const char *host_cv_clip_rect(
    const guest_mem_t *const mem,
    const float x,
    const float y,
    const float width,
    const float height
) {
    (void)mem;
    cv_clip_rect(x, y, width, height);
    return NULL;
}

//===----------------------------------------------------------------------===//
// Paths
//===----------------------------------------------------------------------===//

const char *host_cv_begin_path(const guest_mem_t *const mem) {
    (void)mem;
    cv_begin_path();
    return NULL;
}

const char *host_cv_move_to(const guest_mem_t *const mem, const float x, const float y) {
    (void)mem;
    return cv_move_to(x, y) ? NULL : "mim.cv_move_to: path over 16384 points or 1024 subpaths";
}

const char *host_cv_line_to(const guest_mem_t *const mem, const float x, const float y) {
    (void)mem;
    return cv_line_to(x, y) ? NULL : "mim.cv_line_to: path over 16384 points or 1024 subpaths";
}

const char *host_cv_arc(
    const guest_mem_t *const mem,
    const float x,
    const float y,
    const float radius,
    const float start,
    const float end,
    const uint32_t counterclockwise
) {
    (void)mem;
    // as HTML, which throws; NaN goes on to the canvas, which ignores it
    if (radius < 0.0f) {
        return "mim.cv_arc: negative radius";
    }
    return cv_arc(x, y, radius, start, end, counterclockwise != 0) ? NULL : "mim.cv_arc: path over 16384 points or 1024 subpaths";
}

const char *host_cv_rounded_rect(
    const guest_mem_t *const mem,
    const float x,
    const float y,
    const float width,
    const float height,
    const float radius
) {
    (void)mem;
    return cv_rounded_rect(x, y, width, height, radius) ? NULL : "mim.cv_rounded_rect: path over 16384 points or 1024 subpaths";
}

const char *host_cv_close_path(const guest_mem_t *const mem) {
    (void)mem;
    cv_close_path();
    return NULL;
}

const char *host_cv_fill(const guest_mem_t *const mem) {
    (void)mem;
    cv_fill();
    return NULL;
}

const char *host_cv_stroke(const guest_mem_t *const mem) {
    (void)mem;
    cv_stroke();
    return NULL;
}

const char *host_cv_fill_rect(
    const guest_mem_t *const mem,
    const float x,
    const float y,
    const float width,
    const float height
) {
    (void)mem;
    cv_fill_rect(x, y, width, height);
    return NULL;
}

//===----------------------------------------------------------------------===//
// Images and text
//===----------------------------------------------------------------------===//

const char *host_cv_image_load(const guest_mem_t *const mem, const uint8_t *const png, const uint32_t png_len, uint32_t *const result) {
    (void)mem;
    *result = cv_image_load(png, png_len);
    return NULL;
}

const char *host_cv_draw_image(
    const guest_mem_t *const mem,
    const uint32_t image,
    const float x,
    const float y,
    const float width,
    const float height
) {
    (void)mem;
    return cv_draw_image(image, x, y, width, height) ? NULL : "mim.cv_draw_image: no such image";
}

const char *host_cv_fill_text(
    const guest_mem_t *const mem,
    const uint8_t *const text,
    const uint32_t text_len,
    const float x,
    const float y
) {
    (void)mem;
    cv_fill_text(text, text_len, x, y);
    return NULL;
}

const char *host_cv_measure_text(const guest_mem_t *const mem, const uint8_t *const text, const uint32_t text_len, float *const result) {
    (void)mem;
    *result = cv_measure_text(text, text_len);
    return NULL;
}
