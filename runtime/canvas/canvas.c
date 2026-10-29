//===-- canvas.c - The software canvas: state, paths, fills, strokes ------===//
//
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// Paths are kept in device space: each point is transformed when it's
// added, as HTML does. Curves become lines then, fine enough that no point
// of the curve is more than a quarter pixel off. A stroke becomes polygons
// (one per segment and one per corner) that raster.c fills together.
//
//===----------------------------------------------------------------------===//

#include "runtime/canvas/canvas_impl.h"

#include <assert.h>
#include <math.h>
#include <string.h>

// ponytail: one canvas at a time, as M5 (cg_set_context). The imports have no context of their own
// to find a second one by.
cv_context_t *cv_current;

// How far a flattened curve may be from the real one, in device pixels
static const float cv_tolerance = 0.25f;
// Segments in one arc at most, however big
static const uint32_t cv_max_arc_segments = 512;
// Corners sharper than this ratio of miter length to line width are bevelled, as HTML's default
static const float cv_miter_limit = 10.0f;
static const float cv_pi = 3.14159265358979f;

static bool finite2(const float a, const float b) {
    return isfinite(a) && isfinite(b);
}

static bool finite4(const float a, const float b, const float c, const float d) {
    return finite2(a, b) && finite2(c, d);
}

//===----------------------------------------------------------------------===//
// Set-up
//===----------------------------------------------------------------------===//

void cv_init(
    cv_context_t *const ctx,
    uint32_t *const pixels,
    const uint32_t width,
    const uint32_t height,
    mem_budget_t *const budget
) {
    assert(width > 0 && height > 0 && width <= 65536 && height <= 65536);
    cv_impl_t *const cv = mem_alloc(budget, sizeof(cv_impl_t), "canvas");
    memset(cv, 0, sizeof(*cv));
    cv->budget = budget;
    cv->pixels = pixels;
    cv->width = width;
    cv->height = height;
    cv->state = (cv_state_t){
        .sx = 1.0f,
        .sy = 1.0f,
        .fill = 0xFF000000,
        .stroke = 0xFF000000,
        .line_width = 1.0f,
        .font_size = 10.0f,
        .clip_x1 = width,
        .clip_y1 = height,
    };
    cv->points = mem_alloc(budget, cv_max_points * sizeof(cv_point_t), "canvas path");
    cv->scratch = mem_alloc(budget, cv_max_points * sizeof(cv_point_t), "canvas stroke");
    cv->subpaths = mem_alloc(budget, cv_max_subpaths * sizeof(cv_subpath_t), "canvas subpaths");
    const size_t cells = (size_t)(width + 2) * height * sizeof(float);
    cv->cells = mem_alloc(budget, cells, "canvas cells");
    memset(cv->cells, 0, cells);
    cv->dirty_y0 = UINT32_MAX;
    cv->dirty_x0 = UINT32_MAX;
    cv_text_init(cv);
    ctx->internal.impl = cv;
}

void cv_shutdown(cv_context_t *const ctx) {
    cv_impl_t *const cv = ctx->internal.impl;
    mem_budget_t *const budget = cv->budget;
    for (uint32_t i = 0; i < cv_max_images; i++) {
        if (cv->images[i].pixels != NULL) {
            mem_free(budget, cv->images[i].pixels);
        }
    }
    mem_free(budget, cv->atlas);
    mem_free(budget, cv->glyphs);
    mem_free(budget, cv->cells);
    mem_free(budget, cv->subpaths);
    mem_free(budget, cv->scratch);
    mem_free(budget, cv->points);
    mem_free(budget, cv);
    ctx->internal.impl = NULL;
    if (cv_current == ctx) {
        cv_current = NULL;
    }
}

void cv_set_context(cv_context_t *const ctx) {
    cv_current = ctx;
}

//===----------------------------------------------------------------------===//
// State
//===----------------------------------------------------------------------===//

bool cv_save(void) {
    cv_impl_t *const cv = cv_impl();
    if (cv->depth == cv_max_saves) {
        return false;
    }
    cv->saved[cv->depth++] = cv->state;
    return true;
}

void cv_restore(void) {
    cv_impl_t *const cv = cv_impl();
    if (cv->depth > 0) {
        cv->state = cv->saved[--cv->depth];
    }
}

void cv_translate(const float x, const float y) {
    cv_state_t *const s = &cv_impl()->state;
    if (finite2(x, y)) {
        s->tx += x * s->sx;
        s->ty += y * s->sy;
    }
}

void cv_scale(const float x, const float y) {
    cv_state_t *const s = &cv_impl()->state;
    if (finite2(x, y)) {
        s->sx *= x;
        s->sy *= y;
    }
}

void cv_fill_style(const uint32_t color) {
    cv_impl()->state.fill = color;
}

void cv_stroke_style(const uint32_t color) {
    cv_impl()->state.stroke = color;
}

void cv_line_width(const float width) {
    if (isfinite(width) && width > 0.0f) {
        cv_impl()->state.line_width = width;
    }
}

void cv_font_size(const float size) {
    if (isfinite(size) && size > 0.0f) {
        cv_impl()->state.font_size = size;
    }
}

void cv_clip_rect(const float x, const float y, const float width, const float height) {
    cv_impl_t *const cv = cv_impl();
    cv_state_t *const s = &cv->state;
    if (!finite4(x, y, width, height)) {
        return;
    }
    const cv_point_t a = cv_to_device(s, x, y);
    const cv_point_t b = cv_to_device(s, x + width, y + height);
    const uint32_t x0 = cv_pixel_edge(fminf(a.x, b.x), cv->width);
    const uint32_t x1 = cv_pixel_edge(fmaxf(a.x, b.x), cv->width);
    const uint32_t y0 = cv_pixel_edge(fminf(a.y, b.y), cv->height);
    const uint32_t y1 = cv_pixel_edge(fmaxf(a.y, b.y), cv->height);
    s->clip_x0 = x0 > s->clip_x0 ? x0 : s->clip_x0;
    s->clip_y0 = y0 > s->clip_y0 ? y0 : s->clip_y0;
    s->clip_x1 = x1 < s->clip_x1 ? x1 : s->clip_x1;
    s->clip_y1 = y1 < s->clip_y1 ? y1 : s->clip_y1;
    // an empty clip stays empty, with x0 <= x1
    if (s->clip_x1 < s->clip_x0) {
        s->clip_x1 = s->clip_x0;
    }
    if (s->clip_y1 < s->clip_y0) {
        s->clip_y1 = s->clip_y0;
    }
}

//===----------------------------------------------------------------------===//
// Paths
//===----------------------------------------------------------------------===//

void cv_begin_path(void) {
    cv_impl_t *const cv = cv_impl();
    cv->point_count = 0;
    cv->subpath_count = 0;
}

static cv_subpath_t *last_subpath(cv_impl_t *const cv) {
    return cv->subpath_count == 0 ? NULL : &cv->subpaths[cv->subpath_count - 1];
}

// Starts a subpath at the device point p; false if the path is full
static bool start_subpath(cv_impl_t *const cv, const cv_point_t p) {
    cv_subpath_t *const last = last_subpath(cv);
    // a subpath that's only a move is replaced, as in HTML
    if (last != NULL && last->count == 1 && !last->closed) {
        cv->points[last->first] = p;
        return true;
    }
    if (cv->subpath_count == cv_max_subpaths || cv->point_count == cv_max_points) {
        return false;
    }
    cv->subpaths[cv->subpath_count++] = (cv_subpath_t){cv->point_count, 1, false};
    cv->points[cv->point_count++] = p;
    return true;
}

// Adds the device point p to the open subpath, starting one if there's none; false if full
static bool add_point(cv_impl_t *const cv, const cv_point_t p) {
    cv_subpath_t *const last = last_subpath(cv);
    if (last == NULL) {
        return start_subpath(cv, p);
    }
    if (last->closed) {
        // after a close, the next line starts where the closed subpath did
        const cv_point_t start = cv->points[last->first];
        if (!start_subpath(cv, start)) {
            return false;
        }
        return add_point(cv, p);
    }
    if (cv->point_count == cv_max_points) {
        return false;
    }
    cv->points[cv->point_count++] = p;
    last->count++;
    return true;
}

bool cv_move_to(const float x, const float y) {
    cv_impl_t *const cv = cv_impl();
    return !finite2(x, y) || start_subpath(cv, cv_to_device(&cv->state, x, y));
}

bool cv_line_to(const float x, const float y) {
    cv_impl_t *const cv = cv_impl();
    return !finite2(x, y) || add_point(cv, cv_to_device(&cv->state, x, y));
}

void cv_close_path(void) {
    cv_subpath_t *const last = last_subpath(cv_impl());
    if (last != NULL) {
        last->closed = true;
    }
}

// Segments for an arc of `sweep` radians on a circle of device radius r
static uint32_t arc_segments(const float r, const float sweep) {
    if (r <= cv_tolerance) {
        return 1;
    }
    // the angle whose chord is cv_tolerance from the circle
    const float step = 2.0f * acosf(1.0f - cv_tolerance / r);
    const float n = ceilf(fabsf(sweep) / step);
    // NaN (a zero sweep over a zero step) gives 1, like anything else under 1
    if (!(n >= 1.0f)) {
        return 1;
    }
    return n > (float)cv_max_arc_segments ? cv_max_arc_segments : (uint32_t)n;
}

// Adds the arc as points; the first joins the path with a line, as in HTML
static bool add_arc(
    cv_impl_t *const cv,
    const float x,
    const float y,
    const float r,
    const float start,
    const float sweep
) {
    const cv_state_t *const s = &cv->state;
    const float device_r = r * fmaxf(fabsf(s->sx), fabsf(s->sy));
    const uint32_t n = arc_segments(device_r, sweep);
    for (uint32_t i = 0; i <= n; i++) {
        const float a = start + sweep * (float)i / (float)n;
        if (!add_point(cv, cv_to_device(s, x + r * cosf(a), y + r * sinf(a)))) {
            return false;
        }
    }
    return true;
}

bool cv_arc(
    const float x,
    const float y,
    const float radius,
    const float start,
    const float end,
    const bool counterclockwise
) {
    cv_impl_t *const cv = cv_impl();
    assert(!(radius < 0.0f) && "the caller checks");
    if (!finite4(x, y, radius, start) || !isfinite(end)) {
        return true;
    }
    // HTML's rule: a sweep of a full turn or more is a whole circle; otherwise the sweep is
    // brought into [0, 2π) in the arc's direction
    const float turn = 2.0f * cv_pi;
    float sweep = end - start;
    if (!counterclockwise) {
        sweep = sweep >= turn ? turn : fmodf(fmodf(sweep, turn) + turn, turn);
    } else {
        sweep = -sweep >= turn ? -turn : -fmodf(fmodf(-sweep, turn) + turn, turn);
    }
    return add_arc(cv, x, y, radius, start, sweep);
}

bool cv_rounded_rect(
    const float x,
    const float y,
    const float width,
    const float height,
    const float radius
) {
    cv_impl_t *const cv = cv_impl();
    if (!finite4(x, y, width, height) || !isfinite(radius)) {
        return true;
    }
    const float x0 = fminf(x, x + width);
    const float y0 = fminf(y, y + height);
    const float w = fabsf(width);
    const float h = fabsf(height);
    const float r = fminf(fmaxf(radius, 0.0f), 0.5f * fminf(w, h));
    const float q = 0.5f * cv_pi;
    // clockwise on screen from the top edge: top right, bottom right, bottom left, top left
    if (!start_subpath(cv, cv_to_device(&cv->state, x0 + r, y0))) {
        return false;
    }
    const bool ok = add_arc(cv, x0 + w - r, y0 + r, r, -q, q)
                    && add_arc(cv, x0 + w - r, y0 + h - r, r, 0.0f, q)
                    && add_arc(cv, x0 + r, y0 + h - r, r, q, q)
                    && add_arc(cv, x0 + r, y0 + r, r, 2.0f * q, q);
    if (ok) {
        cv_close_path();
    }
    return ok;
}

void cv_fill(void) {
    cv_impl_t *const cv = cv_impl();
    for (uint32_t i = 0; i < cv->subpath_count; i++) {
        const cv_subpath_t *const sub = &cv->subpaths[i];
        if (sub->count < 3) {
            continue;
        }
        const cv_point_t *const p = cv->points + sub->first;
        for (uint32_t j = 0; j < sub->count; j++) {
            cv_raster_line(cv, p[j], p[(j + 1) % sub->count]);
        }
    }
    cv_raster_paint(cv, cv->state.fill);
}

void cv_fill_rect(const float x, const float y, const float width, const float height) {
    cv_impl_t *const cv = cv_impl();
    if (!finite4(x, y, width, height)) {
        return;
    }
    const cv_state_t *const s = &cv->state;
    const cv_point_t corners[4] = {
        cv_to_device(s, x, y),
        cv_to_device(s, x + width, y),
        cv_to_device(s, x + width, y + height),
        cv_to_device(s, x, y + height),
    };
    cv_raster_polygon(cv, corners, 4);
    cv_raster_paint(cv, s->fill);
}

//===----------------------------------------------------------------------===//
// Strokes
//===----------------------------------------------------------------------===//

static cv_point_t add(const cv_point_t a, const cv_point_t b) {
    return (cv_point_t){a.x + b.x, a.y + b.y};
}

static cv_point_t scaled(const cv_point_t a, const float k) {
    return (cv_point_t){a.x * k, a.y * k};
}

// True if a and b are too close for a direction between them
static bool same_point(const cv_point_t a, const cv_point_t b) {
    return fabsf(a.x - b.x) + fabsf(a.y - b.y) < 1e-3f;
}

// The unit vector from a to b, which mustn't be the same point
static cv_point_t direction(const cv_point_t a, const cv_point_t b) {
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    const float len = sqrtf(dx * dx + dy * dy);
    return (cv_point_t){dx / len, dy / len};
}

// Turned a quarter, to the right on screen
static cv_point_t normal(const cv_point_t d) {
    return (cv_point_t){-d.y, d.x};
}

// Fills the outside of the corner at v between the segments going d0 then d1, half-width hw
static void stroke_corner(
    cv_impl_t *const cv,
    const cv_point_t v,
    const cv_point_t d0,
    const cv_point_t d1,
    const float hw
) {
    const float cross = d0.x * d1.y - d0.y * d1.x;
    const float dot = d0.x * d1.x + d0.y * d1.y;
    if (fabsf(cross) < 1e-6f && dot > 0.0f) {
        return; // straight on: the segments already meet
    }
    // the outside of the turn is away from where it turns
    const float side = cross > 0.0f ? -hw : hw;
    const cv_point_t a = add(v, scaled(normal(d0), side));
    const cv_point_t b = add(v, scaled(normal(d1), side));
    // the miter tip is 1 / cos(θ / 2) half-widths out, along the sum of the normals
    const float ratio = sqrtf(2.0f / fmaxf(1.0f + dot, 1e-12f));
    if (ratio <= cv_miter_limit) {
        const cv_point_t m = add(v, scaled(add(normal(d0), normal(d1)), side / (1.0f + dot)));
        const cv_point_t quad[4] = {v, a, m, b};
        cv_raster_polygon(cv, quad, 4);
    } else {
        const cv_point_t bevel[3] = {v, a, b};
        cv_raster_polygon(cv, bevel, 3);
    }
}

// Strokes one subpath: a rectangle per segment, then the corners
static void stroke_subpath(cv_impl_t *const cv, const cv_subpath_t *const sub, const float hw) {
    // points closer than this have no direction to speak of
    cv_point_t *const p = cv->scratch;
    uint32_t n = 0;
    for (uint32_t i = 0; i < sub->count; i++) {
        const cv_point_t q = cv->points[sub->first + i];
        if (n == 0 || !same_point(q, p[n - 1])) {
            p[n++] = q;
        }
    }
    const bool closed = sub->closed && n > 2;
    if (closed && same_point(p[n - 1], p[0])) {
        n--;
    }
    if (n < 2) {
        return;
    }
    const uint32_t segments = closed ? n : n - 1;
    for (uint32_t i = 0; i < segments; i++) {
        const cv_point_t a = p[i];
        const cv_point_t b = p[(i + 1) % n];
        const cv_point_t off = scaled(normal(direction(a, b)), hw);
        const cv_point_t quad[4] = {add(a, off), add(b, off), add(b, scaled(off, -1.0f)), add(a, scaled(off, -1.0f))};
        cv_raster_polygon(cv, quad, 4);
    }
    for (uint32_t i = closed ? 0 : 1; i < (closed ? n : n - 1); i++) {
        const cv_point_t prev = p[(i + n - 1) % n];
        const cv_point_t next = p[(i + 1) % n];
        stroke_corner(cv, p[i], direction(prev, p[i]), direction(p[i], next), hw);
    }
}

void cv_stroke(void) {
    cv_impl_t *const cv = cv_impl();
    const float hw = fminf(0.5f * cv->state.line_width * cv_mean_scale(&cv->state), cv_coord_max);
    for (uint32_t i = 0; i < cv->subpath_count; i++) {
        stroke_subpath(cv, &cv->subpaths[i], hw);
    }
    cv_raster_paint(cv, cv->state.stroke);
}

//===----------------------------------------------------------------------===//
// Images
//===----------------------------------------------------------------------===//

uint32_t cv_image_load(const uint8_t *const data, const size_t size) {
    cv_impl_t *const cv = cv_impl();
    uint32_t slot = 0;
    while (slot < cv_max_images && cv->images[slot].pixels != NULL) {
        slot++;
    }
    if (slot == cv_max_images) {
        return 0;
    }
    cv_image_t *const image = &cv->images[slot];
    image->pixels = cv_decode_png(cv->budget, data, size, &image->width, &image->height);
    return image->pixels == NULL ? 0 : slot + 1;
}

bool cv_draw_image(
    const uint32_t image,
    const float x,
    const float y,
    const float width,
    const float height
) {
    cv_impl_t *const cv = cv_impl();
    if (image == 0 || image > cv_max_images || cv->images[image - 1].pixels == NULL) {
        return false;
    }
    if (finite4(x, y, width, height)) {
        const cv_state_t *const s = &cv->state;
        // the image's top left and bottom right corners; a mirroring transform swaps them
        cv_raster_image(cv, &cv->images[image - 1], cv_to_device(s, x, y), cv_to_device(s, x + width, y + height));
    }
    return true;
}
