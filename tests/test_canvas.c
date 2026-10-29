// SPDX-License-Identifier: MIT OR GPL-2.0-only

#include "mim_test.h"
#include "runtime/canvas/canvas.h"

#include <float.h>
#include <math.h>
#include <stdbool.h>
#include <string.h>

enum {
    w = 8,
    h = 4,
    // sentinel pixels on each side of the canvas, to catch a write outside it
    guard = 64,
};

static const uint32_t bg = 0x00123456;
static const uint32_t sentinel = 0xDEADBEEF;

static uint32_t buffer[guard + w * h + guard];
static uint32_t *const pixels = buffer + guard;
static cv_context_t ctx;

// A fresh canvas of `bg`, filling opaque white
static void reset(void) {
    for (size_t i = 0; i < sizeof(buffer) / sizeof(buffer[0]); i++) {
        buffer[i] = sentinel;
    }
    for (size_t i = 0; i < w * h; i++) {
        pixels[i] = bg;
    }
    cv_init(&ctx, pixels, w, h);
    cv_set_context(&ctx);
    cv_fill_style(0xFFFFFFFF);
}

// Checks the canvas against `map`, one row per line: '.' is bg, '#' is `fg`, anything else neither.
// Also checks the sentinels are untouched.
static void expect(const int line, const uint32_t fg, const char *const map) {
    char got[(w + 1) * h + 1];
    char *p = got;
    for (size_t y = 0; y < h; y++) {
        for (size_t x = 0; x < w; x++) {
            const uint32_t c = pixels[y * w + x];
            *p++ = c == bg ? '.' : (c == fg ? '#' : '?');
        }
        *p++ = '\n';
    }
    *p = '\0';
    if (strcmp(got, map) != 0) {
        fprintf(stderr, "line %d: expected\n%sgot\n%s", line, map, got);
        CHECK(false);
    }
    for (size_t i = 0; i < guard; i++) {
        CHECK(buffer[i] == sentinel && buffer[guard + w * h + i] == sentinel);
    }
}

// A pixel is filled if its centre (x + 0.5, y + 0.5) is inside, the left and top edges included
static void test_pixel_centres(void) {
    reset();
    cv_fill_rect(1, 1, 3, 2);
    expect(__LINE__, 0x00FFFFFF,
           "........\n"
           ".###....\n"
           ".###....\n"
           "........\n");

    // centres 1.5, 2.5 and 3.5 are in [1.5, 3.6), 4.5 isn't. Row 0's centre is on the top edge.
    reset();
    cv_fill_rect(1.5f, 0.5f, 2.1f, 1.0f);
    expect(__LINE__, 0x00FFFFFF,
           ".###....\n"
           "........\n"
           "........\n"
           "........\n");

    // smaller than a pixel and around no centre: nothing
    reset();
    cv_fill_rect(2.6f, 1.6f, 0.8f, 0.8f);
    expect(__LINE__, 0x00FFFFFF,
           "........\n"
           "........\n"
           "........\n"
           "........\n");
}

// Rectangles that share a fractional edge: every pixel is filled once, so no double blend and no gap
static void test_shared_edge(void) {
    reset();
    cv_fill_style(0x80FFFFFF);
    cv_fill_rect(0, 0, 2.7f, 1);
    cv_fill_rect(2.7f, 0, 5.3f, 1);
    expect(__LINE__, 0x00899AAB,
           "########\n"
           "........\n"
           "........\n"
           "........\n");
}

static void test_colour(void) {
    // the fill starts opaque black, and the top byte is never written
    reset();
    cv_init(&ctx, pixels, w, h);
    cv_fill_rect(0, 0, 1, 1);
    CHECK(pixels[0] == 0x00000000);

    // half white over black rounds to 0x80, and over white stays white
    reset();
    pixels[0] = 0x00000000;
    pixels[1] = 0x00FFFFFF;
    cv_fill_style(0x80FFFFFF);
    cv_fill_rect(0, 0, 2, 1);
    CHECK(pixels[0] == 0x00808080 && pixels[1] == 0x00FFFFFF);

    // each channel on its own: 0x40 of red over bg
    reset();
    cv_fill_style(0x40FF0000);
    cv_fill_rect(0, 0, 1, 1);
    const uint32_t r = (0xFF * 0x40 + 0x12 * 0xBF + 127) / 255;
    const uint32_t g = (0x34 * 0xBF + 127) / 255;
    const uint32_t b = (0x56 * 0xBF + 127) / 255;
    CHECK(pixels[0] == (r << 16 | g << 8 | b));

    // alpha 0 draws nothing
    reset();
    cv_fill_style(0x00FFFFFF);
    cv_fill_rect(0, 0, w, h);
    CHECK(pixels[0] == bg);
}

// Floats straight from a guest: none may write outside the canvas, or hit undefined behaviour
// converting to an integer
static void test_wild_floats(void) {
    const float inf = INFINITY;
    const float nan = NAN;
    static const char *const nothing = "........\n........\n........\n........\n";
    static const char *const all = "########\n########\n########\n########\n";
    const struct {
        int line;
        float x, y, width, height;
        const char *map;
    } cases[] = {
        {__LINE__, nan, 0, 4, 4, nothing},
        {__LINE__, 0, 0, nan, 4, nothing},
        {__LINE__, 0, nan, 4, 4, nothing},
        {__LINE__, 0, 0, 4, nan, nothing},
        {__LINE__, 0, 0, inf, inf, all},
        {__LINE__, -inf, -inf, inf, inf, nothing}, // -inf + inf is NaN
        {__LINE__, -inf, 0, 4, 4, nothing},
        {__LINE__, -1e30f, -1e30f, 2e30f, 2e30f, all},
        {__LINE__, -FLT_MAX, -FLT_MAX, FLT_MAX, FLT_MAX, nothing}, // ends at 0
        {__LINE__, 1e30f, 1e30f, 4, 4, nothing},
        {__LINE__, 4, 2, -2, -1, nothing}, // a negative size, as M5
        {__LINE__, 7.6f, 0, 100, 100, nothing}, // starts past the last centre
        {__LINE__, -100, 3.4f, 1e9f, 1, "........\n........\n........\n########\n"},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        reset();
        cv_fill_rect(cases[i].x, cases[i].y, cases[i].width, cases[i].height);
        expect(cases[i].line, 0x00FFFFFF, cases[i].map);
    }
}

// Drawing goes to whichever canvas is picked, and only there
static void test_set_context(void) {
    reset();
    uint32_t other_pixels[2 * 2] = {bg, bg, bg, bg};
    cv_context_t other;
    cv_init(&other, other_pixels, 2, 2);
    cv_set_context(&other);
    cv_fill_rect(0, 0, 100, 100);
    CHECK(other_pixels[0] == 0 && other_pixels[3] == 0); // its own fill: opaque black, as it started
    cv_fill_style(0xFF00FF00);
    cv_fill_rect(1, 1, 1, 1);
    CHECK(other_pixels[3] == 0x0000FF00 && other_pixels[0] == 0);
    expect(__LINE__, 0x00FFFFFF, "........\n........\n........\n........\n");
    cv_set_context(NULL);
}

int main(void) {
    test_pixel_centres();
    test_shared_edge();
    test_colour();
    test_wild_floats();
    test_set_context();
    return 0;
}
