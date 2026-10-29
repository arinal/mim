//===-- host_cv.c - The mim.cv_* imports: the guest's canvas --------------===//
//
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// The imports that can trap: a limit hit, a negative radius, an image that
// isn't one. The rest call the canvas cv_set_context picked straight from
// the generated thunks; it already handles any float a guest sends.
//
//===----------------------------------------------------------------------===//

#include "runtime/canvas/canvas.h"
#include "runtime/guest/ffi.h"

const char *host_cv_save(const guest_mem_t *const mem) {
    (void)mem;
    return cv_save() ? NULL : "mim.cv_save: more than 32 saves";
}

//===----------------------------------------------------------------------===//
// Paths
//===----------------------------------------------------------------------===//

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

//===----------------------------------------------------------------------===//
// Images and text
//===----------------------------------------------------------------------===//

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

