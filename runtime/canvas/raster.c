//===-- raster.c - Filling polygons with exact-area anti-aliasing ---------===//
//
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// Each line adds, to every pixel it crosses, the signed area it covers to
// its right; walking a row and summing gives each pixel's winding number,
// already weighted by how much of the pixel is inside. min(1, |sum|) is the
// coverage by the non-zero rule. The same algorithm as font-rs and
// stb_truetype's rasteriser.
//
// ponytail: inside one pixel, the areas add before |sum| is taken. Shapes
// going opposite ways round that meet in a pixel partly cancel there (a
// light seam), and partial overlaps count twice (a slightly heavy edge).
// Neither happens in UI shapes or glyphs; exact non-zero would need winding
// sampled at points inside each pixel.
//
//===----------------------------------------------------------------------===//

#include "runtime/canvas/canvas_impl.h"

#include <math.h>

// min and max for finite floats; unlike fminf and fmaxf, which in ISO C mode are calls into libm
static float lesser(const float a, const float b) {
    return a < b ? a : b;
}

static float greater(const float a, const float b) {
    return a > b ? a : b;
}

// Adds the line from (x0, y0) to (x1, y1), both with 0 <= x <= width, to the rows inside the clip
static void line_inside(
    cv_impl_t *const cv,
    float x0,
    float y0,
    float x1,
    float y1
) {
    if (y0 == y1) {
        return;
    }
    float dir = 1.0f;
    if (y0 > y1) {
        float t = x0;
        x0 = x1;
        x1 = t;
        t = y0;
        y0 = y1;
        y1 = t;
        dir = -1.0f;
    }
    const float top = greater(y0, (float)cv->state.clip_y0);
    const float bottom = lesser(y1, (float)cv->state.clip_y1);
    if (top >= bottom) {
        return;
    }
    const float dxdy = (x1 - x0) / (y1 - y0);
    const uint32_t stride = cv->width + 2;
    float x = lesser(greater(x0 + (top - y0) * dxdy, 0.0f), (float)cv->width);
    const uint32_t row_end = (uint32_t)ceilf(bottom);
    for (uint32_t y = (uint32_t)top; y < row_end; y++) {
        float *const row = cv->cells + (size_t)y * stride;
        const float dy = lesser((float)y + 1.0f, bottom) - greater((float)y, top);
        // the float error in x accumulates; keep it on the canvas
        const float x_next = lesser(greater(x + dxdy * dy, 0.0f), (float)cv->width);
        const float d = dy * dir;
        const float xa = lesser(x, x_next);
        const float xb = greater(x, x_next);
        const float xa_floor = floorf(xa);
        const float xb_ceil = ceilf(xb);
        const uint32_t ia = (uint32_t)xa_floor;
        const uint32_t ib = (uint32_t)xb_ceil;
        if (ib <= ia + 1) {
            // within one pixel: split by where the line crosses it on average
            const float mid = 0.5f * (x + x_next) - xa_floor;
            row[ia] += d - d * mid;
            row[ia + 1] += d * mid;
        } else {
            const float s = 1.0f / (xb - xa);
            const float fa = xa - xa_floor;
            const float a0 = 0.5f * s * (1.0f - fa) * (1.0f - fa);
            const float fb = xb - xb_ceil + 1.0f;
            const float am = 0.5f * s * fb * fb;
            row[ia] += d * a0;
            if (ib == ia + 2) {
                row[ia + 1] += d * (1.0f - a0 - am);
            } else {
                const float a1 = s * (1.5f - fa);
                row[ia + 1] += d * (a1 - a0);
                for (uint32_t i = ia + 2; i < ib - 1; i++) {
                    row[i] += d * s;
                }
                const float a2 = a1 + (float)(ib - ia - 3) * s;
                row[ib - 1] += d * (1.0f - a2 - am);
            }
            row[ib] += d * am;
        }
        x = x_next;
        if (y < cv->dirty_y0) {
            cv->dirty_y0 = y;
        }
        if (y + 1 > cv->dirty_y1) {
            cv->dirty_y1 = y + 1;
        }
        if (ia < cv->dirty_x0) {
            cv->dirty_x0 = ia;
        }
        if (ib + 2 > cv->dirty_x1) {
            cv->dirty_x1 = ib + 2;
        }
    }
}

void cv_raster_line(cv_impl_t *const cv, cv_point_t a, cv_point_t b) {
    // the only way into the cells, so nothing a guest does can put NaN or infinity there
    a = (cv_point_t){cv_clamp_coord(a.x), cv_clamp_coord(a.y)};
    b = (cv_point_t){cv_clamp_coord(b.x), cv_clamp_coord(b.y)};
    // Cut where the line crosses x = 0 and x = width. The pieces outside move onto that edge: off
    // the left they still change the winding of every pixel to their right; off the right, of none.
    const float w = (float)cv->width;
    float cuts[4] = {0.0f, 1.0f, 1.0f, 1.0f};
    uint32_t count = 1;
    if ((a.x < 0.0f) != (b.x < 0.0f)) {
        cuts[count++] = -a.x / (b.x - a.x);
    }
    if ((a.x > w) != (b.x > w)) {
        cuts[count++] = (w - a.x) / (b.x - a.x);
    }
    if (count == 3 && cuts[1] > cuts[2]) {
        const float t = cuts[1];
        cuts[1] = cuts[2];
        cuts[2] = t;
    }
    cuts[count] = 1.0f;
    float px = fminf(fmaxf(a.x, 0.0f), w);
    float py = a.y;
    for (uint32_t i = 1; i <= count; i++) {
        const float t = cuts[i];
        const float nx = i == count ? b.x : a.x + (b.x - a.x) * t;
        const float ny = i == count ? b.y : a.y + (b.y - a.y) * t;
        const float cx = fminf(fmaxf(nx, 0.0f), w);
        line_inside(cv, px, py, cx, ny);
        px = cx;
        py = ny;
    }
}

void cv_raster_polygon(cv_impl_t *const cv, const cv_point_t *const points, const uint32_t count) {
    // twice the signed area: every polygon goes the same way round, so overlaps add
    float area = 0.0f;
    for (uint32_t i = 0; i < count; i++) {
        const cv_point_t p = points[i];
        const cv_point_t q = points[(i + 1) % count];
        area += p.x * q.y - q.x * p.y;
    }
    for (uint32_t i = 0; i < count; i++) {
        const cv_point_t p = points[i];
        const cv_point_t q = points[(i + 1) % count];
        if (area >= 0.0f) {
            cv_raster_line(cv, p, q);
        } else {
            cv_raster_line(cv, q, p);
        }
    }
}

void cv_blend(uint32_t *const pixel, const uint32_t rgb, const uint32_t alpha) {
    if (alpha >= 255) {
        *pixel = rgb & 0x00FFFFFF;
        return;
    }
    const uint32_t dst = *pixel;
    uint32_t out = 0;
    for (uint32_t shift = 0; shift < 24; shift += 8) {
        const uint32_t s = (rgb >> shift) & 0xFF;
        const uint32_t d = (dst >> shift) & 0xFF;
        out |= ((s * alpha + d * (255 - alpha) + 127) / 255) << shift;
    }
    *pixel = out;
}

void cv_blend_premultiplied(uint32_t *const pixel, const uint32_t argb) {
    const uint32_t alpha = argb >> 24;
    const uint32_t dst = *pixel;
    uint32_t out = 0;
    for (uint32_t shift = 0; shift < 24; shift += 8) {
        const uint32_t s = (argb >> shift) & 0xFF;
        const uint32_t d = (dst >> shift) & 0xFF;
        const uint32_t c = s + (d * (255 - alpha) + 127) / 255;
        out |= (c > 255 ? 255 : c) << shift;
    }
    *pixel = out;
}

// The alpha, 0 to 255, for a winding sum by the non-zero rule
static uint32_t coverage_alpha(const float winding, const uint32_t alpha) {
    // comparisons, not fminf: in ISO C mode that's a call into libm for every pixel
    float coverage = winding < 0.0f ? -winding : winding;
    coverage = coverage > 1.0f ? 1.0f : coverage;
    return (uint32_t)(coverage * (float)alpha + 0.5f);
}

void cv_raster_paint(cv_impl_t *const cv, const uint32_t color) {
    const uint32_t stride = cv->width + 2;
    const uint32_t alpha = color >> 24;
    // each row in three parts: left of the clip (summed, not painted), in it, and past it (cleared)
    const uint32_t x_end = cv->dirty_x1 < stride ? cv->dirty_x1 : stride;
    const uint32_t clip_x0 = cv->state.clip_x0;
    const uint32_t paint_x0 = cv->dirty_x0 > clip_x0 ? cv->dirty_x0 : clip_x0;
    const uint32_t paint_x1 = x_end < cv->state.clip_x1 ? x_end : cv->state.clip_x1;
    for (uint32_t y = cv->dirty_y0; y < cv->dirty_y1; y++) {
        float *const row = cv->cells + (size_t)y * stride;
        uint32_t *const out = cv->pixels + (size_t)y * cv->width;
        float winding = 0.0f;
        uint32_t x = cv->dirty_x0;
        for (; x < paint_x0; x++) {
            winding += row[x];
            row[x] = 0.0f;
        }
        // inside a shape a cell is 0, and a pixel's coverage is its left neighbour's; the first
        // one painted starts from what was summed left of the clip
        uint32_t a = coverage_alpha(winding, alpha);
        for (; x < paint_x1; x++) {
            if (row[x] != 0.0f) {
                winding += row[x];
                row[x] = 0.0f;
                a = coverage_alpha(winding, alpha);
            }
            if (a == 255) {
                out[x] = color & 0x00FFFFFF;
            } else if (a != 0) {
                cv_blend(&out[x], color, a);
            }
        }
        for (; x < x_end; x++) {
            row[x] = 0.0f;
        }
    }
    cv->dirty_y0 = UINT32_MAX;
    cv->dirty_y1 = 0;
    cv->dirty_x0 = UINT32_MAX;
    cv->dirty_x1 = 0;
}

// a and b mixed by w out of 256, two 8-bit channels at a time in each half of a 32-bit integer
static uint32_t mix(const uint32_t a, const uint32_t b, const uint32_t w) {
    const uint32_t rb = (((a & 0x00FF00FF) * (256 - w) + (b & 0x00FF00FF) * w) >> 8) & 0x00FF00FF;
    const uint32_t ag = ((((a >> 8) & 0x00FF00FF) * (256 - w) + ((b >> 8) & 0x00FF00FF) * w) >> 8) & 0x00FF00FF;
    return rb | ag << 8;
}

// Where a position (in image pixels) falls between two pixel centres: the pixel before it, and
// how far on to the next, out of 256. Clamped to the image's edge pixels.
static uint32_t between(const float at, const uint32_t size) {
    float f = at - 0.5f;
    f = f < 0.0f ? 0.0f : f;
    f = f > (float)(size - 1) ? (float)(size - 1) : f;
    return (uint32_t)(f * 256.0f);
}

void cv_raster_image(cv_impl_t *const cv, const cv_image_t *const img, const cv_point_t a, const cv_point_t b) {
    const cv_state_t *const s = &cv->state;
    if (a.x == b.x || a.y == b.y) {
        return;
    }
    // ponytail: the image's edges are cut at pixel centres, not anti-aliased
    const uint32_t x0 = cv_pixel_edge(fminf(a.x, b.x), cv->width);
    const uint32_t x1 = cv_pixel_edge(fmaxf(a.x, b.x), cv->width);
    const uint32_t y0 = cv_pixel_edge(fminf(a.y, b.y), cv->height);
    const uint32_t y1 = cv_pixel_edge(fmaxf(a.y, b.y), cv->height);
    const uint32_t cx0 = x0 > s->clip_x0 ? x0 : s->clip_x0;
    const uint32_t cx1 = x1 < s->clip_x1 ? x1 : s->clip_x1;
    const uint32_t cy0 = y0 > s->clip_y0 ? y0 : s->clip_y0;
    const uint32_t cy1 = y1 < s->clip_y1 ? y1 : s->clip_y1;
    const float ku = (float)img->width / (b.x - a.x);
    const float kv = (float)img->height / (b.y - a.y);
    for (uint32_t py = cy0; py < cy1; py++) {
        uint32_t *const out = cv->pixels + (size_t)py * cv->width;
        const uint32_t v = between(((float)py + 0.5f - a.y) * kv, img->height);
        const uint32_t y0 = v >> 8;
        const uint32_t y1 = y0 + 1 < img->height ? y0 + 1 : y0;
        const uint32_t *const row0 = img->pixels + (size_t)y0 * img->width;
        const uint32_t *const row1 = img->pixels + (size_t)y1 * img->width;
        for (uint32_t px = cx0; px < cx1; px++) {
            const uint32_t u = between(((float)px + 0.5f - a.x) * ku, img->width);
            const uint32_t x0 = u >> 8;
            const uint32_t x1 = x0 + 1 < img->width ? x0 + 1 : x0;
            const uint32_t top = mix(row0[x0], row0[x1], u & 0xFF);
            const uint32_t bottom = mix(row1[x0], row1[x1], u & 0xFF);
            const uint32_t c = mix(top, bottom, v & 0xFF);
            if (c >> 24 != 0) {
                cv_blend_premultiplied(&out[px], c);
            }
        }
    }
}

void cv_raster_mask(
    cv_impl_t *const cv,
    const uint8_t *const mask,
    const uint32_t stride,
    const uint32_t width,
    const uint32_t height,
    const int32_t x,
    const int32_t y,
    const uint32_t color
) {
    const cv_state_t *const s = &cv->state;
    const uint32_t alpha = color >> 24;
    // the part of the mask inside the clip, in device pixels; 64 bits, so nothing here wraps
    const int64_t x0 = (int64_t)x > (int64_t)s->clip_x0 ? (int64_t)x : (int64_t)s->clip_x0;
    const int64_t y0 = (int64_t)y > (int64_t)s->clip_y0 ? (int64_t)y : (int64_t)s->clip_y0;
    const int64_t x1 = (int64_t)x + width < (int64_t)s->clip_x1 ? (int64_t)x + width : (int64_t)s->clip_x1;
    const int64_t y1 = (int64_t)y + height < (int64_t)s->clip_y1 ? (int64_t)y + height : (int64_t)s->clip_y1;
    for (int64_t py = y0; py < y1; py++) {
        const uint8_t *const in = mask + (size_t)(py - y) * stride;
        uint32_t *const out = cv->pixels + (size_t)py * cv->width;
        for (int64_t px = x0; px < x1; px++) {
            const uint32_t a = (in[px - x] * alpha + 127) / 255;
            if (a != 0) {
                cv_blend(&out[px], color, a);
            }
        }
    }
}
