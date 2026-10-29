// SPDX-License-Identifier: MIT OR GPL-2.0-only

// The canvas's rules, on an 8 × 4 canvas. What it looks like is in test_golden.

#include "mim_test.h"
#include "runtime/canvas/canvas.h"

#include <float.h>
#include <math.h>
#include <stdbool.h>
#include <string.h>

// test-only PNG writer; static, so it can't clash with the canvas's stb_image
#pragma GCC diagnostic ignored "-Wunused-function"
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include "stb_image_write.h"

enum {
    w = 8,
    h = 4,
    // sentinel pixels on each side of the canvas, to catch a write outside it
    guard = 64,
};

static const uint32_t bg = 0x00123456;
static const uint32_t white = 0x00FFFFFF;
static const uint32_t sentinel = 0xDEADBEEF;

static mem_t mem;
static mem_budget_t budget;
static uint32_t buffer[guard + w * h + guard];
static uint32_t *const pixels = buffer + guard;
static cv_context_t ctx;
static bool open;

// A fresh canvas of `bg`, filling and stroking opaque white
static void reset(void) {
    if (open) {
        cv_shutdown(&ctx);
    }
    CHECK(mem_get_stats(&budget).used_blocks == 0);
    for (size_t i = 0; i < sizeof(buffer) / sizeof(buffer[0]); i++) {
        buffer[i] = sentinel;
    }
    for (size_t i = 0; i < w * h; i++) {
        pixels[i] = bg;
    }
    cv_init(&ctx, pixels, w, h, &budget);
    cv_set_context(&ctx);
    open = true;
    cv_fill_style(0xFF000000 | white);
    cv_stroke_style(0xFF000000 | white);
}

// `fg` over `bg` with alpha a, as the canvas rounds it
static uint32_t over(const uint32_t fg, const uint32_t a) {
    uint32_t out = 0;
    for (uint32_t shift = 0; shift < 24; shift += 8) {
        out |= ((((fg >> shift) & 0xFF) * a + ((bg >> shift) & 0xFF) * (255 - a) + 127) / 255) << shift;
    }
    return out;
}

// Checks nothing was written outside the canvas
static void check_guards(void) {
    for (size_t i = 0; i < guard; i++) {
        CHECK(buffer[i] == sentinel && buffer[guard + w * h + i] == sentinel);
    }
}

// Checks the canvas against `map`, one row per line: '.' is bg, '#' is white, '+' is anything
// else. Also checks the sentinels are untouched.
static void expect(const int line, const char *const map) {
    char got[(w + 1) * h + 1];
    char *p = got;
    for (size_t y = 0; y < h; y++) {
        for (size_t x = 0; x < w; x++) {
            const uint32_t c = pixels[y * w + x];
            *p++ = c == bg ? '.' : (c == white ? '#' : '+');
        }
        *p++ = '\n';
    }
    *p = '\0';
    if (strcmp(got, map) != 0) {
        fprintf(stderr, "line %d: expected\n%sgot\n%s", line, map, got);
        CHECK(false);
    }
    check_guards();
}

static const char nothing[] = "........\n........\n........\n........\n";
static const char all[] = "########\n########\n########\n########\n";

static uint32_t pixel(const uint32_t x, const uint32_t y) {
    return pixels[y * w + x];
}

// A pixel gets the share of its area that's inside
static void test_coverage(void) {
    reset();
    cv_fill_rect(1, 1, 3, 2);
    expect(__LINE__,
           "........\n"
           ".###....\n"
           ".###....\n"
           "........\n");

    // x 1.5 to 3.5, y 0.5 to 1.5: the middle column half covered, the corners a quarter
    reset();
    cv_fill_rect(1.5f, 0.5f, 2, 1);
    expect(__LINE__,
           ".+++....\n"
           ".+++....\n"
           "........\n"
           "........\n");
    CHECK(pixel(1, 0) == over(white, 64) && pixel(2, 0) == over(white, 128) && pixel(3, 1) == over(white, 64));

    // smaller than a pixel: 0.4 × 0.4 of each of four pixels
    reset();
    cv_fill_rect(2.6f, 1.6f, 0.8f, 0.8f);
    expect(__LINE__,
           "........\n"
           "..++....\n"
           "..++....\n"
           "........\n");
    CHECK(pixel(2, 1) == over(white, 41));

    // a negative size goes the other way, as in HTML
    reset();
    cv_fill_rect(4, 2, -2, -1);
    expect(__LINE__,
           "........\n"
           "..##....\n"
           "........\n"
           "........\n");
}

// One fill adds up its shapes before painting: no seam where they meet, overlaps count once,
// and a shape going the other way round cuts a hole
static void test_nonzero(void) {
    // two rectangles meeting at x = 2.7
    reset();
    cv_begin_path();
    cv_rounded_rect(0, 0, 2.7f, 2, 0);
    cv_rounded_rect(2.7f, 0, 5.3f, 2, 0);
    cv_fill();
    expect(__LINE__,
           "########\n"
           "########\n"
           "........\n"
           "........\n");

    // overlapping the same way round: half white, once, everywhere
    reset();
    cv_fill_style(0x80FFFFFF);
    cv_begin_path();
    cv_rounded_rect(0, 0, 6, 4, 0);
    cv_rounded_rect(2, 0, 6, 4, 0);
    cv_fill();
    for (uint32_t x = 0; x < w; x++) {
        CHECK(pixel(x, 0) == over(white, 128));
    }

    // the inner square drawn anticlockwise: winding 1 - 1 = 0 inside
    reset();
    cv_begin_path();
    cv_move_to(0, 0);
    cv_line_to(8, 0);
    cv_line_to(8, 4);
    cv_line_to(0, 4);
    cv_move_to(2, 1);
    cv_line_to(2, 3);
    cv_line_to(6, 3);
    cv_line_to(6, 1);
    cv_fill();
    expect(__LINE__,
           "########\n"
           "##....##\n"
           "##....##\n"
           "########\n");
}

static void test_colour(void) {
    // the fill starts opaque black, and the top byte is never written
    reset();
    cv_shutdown(&ctx);
    cv_init(&ctx, pixels, w, h, &budget);
    cv_set_context(&ctx);
    cv_fill_rect(0, 0, 1, 1);
    CHECK(pixels[0] == 0x00000000);

    // half white over black rounds to 0x80, and over white stays white
    reset();
    pixels[0] = 0x00000000;
    pixels[1] = white;
    cv_fill_style(0x80FFFFFF);
    cv_fill_rect(0, 0, 2, 1);
    CHECK(pixels[0] == 0x00808080 && pixels[1] == white);

    // each channel on its own
    reset();
    cv_fill_style(0x40FF0000);
    cv_fill_rect(0, 0, 1, 1);
    CHECK(pixels[0] == over(0x00FF0000, 0x40));

    // alpha 0 draws nothing
    reset();
    cv_fill_style(0x00FFFFFF);
    cv_fill_rect(0, 0, w, h);
    expect(__LINE__, nothing);
}

// save, restore, transform and clip
static void test_state(void) {
    // the transform applies to what's drawn after it, in its own units
    reset();
    cv_translate(1, 1);
    cv_scale(2, 1);
    cv_fill_rect(0, 0, 1, 2);
    expect(__LINE__,
           "........\n"
           ".##.....\n"
           ".##.....\n"
           "........\n");

    // restore brings back the transform, the fill and the clip
    reset();
    CHECK(cv_save());
    cv_translate(4, 0);
    cv_fill_style(0xFF000000);
    cv_clip_rect(0, 0, 2, 1);
    cv_restore();
    cv_fill_rect(0, 0, 1, 1);
    expect(__LINE__,
           "#.......\n"
           "........\n"
           "........\n"
           "........\n");

    // the clip is whole pixels, by their centres; a second clip narrows the first
    reset();
    cv_clip_rect(1.4f, 0.6f, 5.2f, 10);
    cv_clip_rect(0, 0, 5, 3);
    cv_fill_rect(0, 0, w, h);
    expect(__LINE__,
           "........\n"
           ".####...\n"
           ".####...\n"
           "........\n");

    // 32 saves fit, the 33rd doesn't; restores past the bottom do nothing
    reset();
    for (int i = 0; i < cv_max_saves; i++) {
        CHECK(cv_save());
    }
    CHECK(!cv_save());
    for (int i = 0; i < cv_max_saves + 5; i++) {
        cv_restore();
    }
    CHECK(cv_save());
}

static void test_paths(void) {
    // after close_path, a line_to starts from where the closed subpath did: two triangles, both
    // clockwise, that make a rectangle. From anywhere else, the second would have no area.
    reset();
    cv_begin_path();
    cv_move_to(0, 0);
    cv_line_to(4, 0);
    cv_line_to(4, 2);
    cv_close_path();
    cv_line_to(4, 2);
    cv_line_to(0, 2);
    cv_fill();
    expect(__LINE__,
           "####....\n"
           "####....\n"
           "........\n"
           "........\n");

    // an edge that comes in across the left side, from (-4, 0) to (4, 4): it enters at y = 2, so
    // nothing above row 2 is covered. Bent to start at (0, 0), it would cover half of row 1.
    reset();
    cv_begin_path();
    cv_move_to(-4, 0);
    cv_line_to(4, 4);
    cv_line_to(-4, 4);
    cv_fill();
    expect(__LINE__,
           "........\n"
           "........\n"
           "++......\n"
           "##++....\n");

    // a 2-wide stroke of y = 2: rows 1 and 2, cut square at both ends
    reset();
    cv_line_width(2);
    cv_begin_path();
    cv_move_to(1, 2);
    cv_line_to(7, 2);
    cv_stroke();
    expect(__LINE__,
           "........\n"
           ".######.\n"
           ".######.\n"
           "........\n");

    // a closed square: the outline is a whole ring, corners included
    reset();
    cv_begin_path();
    cv_move_to(1.5f, 0.5f);
    cv_line_to(6.5f, 0.5f);
    cv_line_to(6.5f, 3.5f);
    cv_line_to(1.5f, 3.5f);
    cv_close_path();
    cv_stroke();
    expect(__LINE__,
           ".######.\n"
           ".#....#.\n"
           ".#....#.\n"
           ".######.\n");

    // a zero radius arc is a point; a full turn or more is a circle
    reset();
    cv_begin_path();
    cv_arc(4, 2, 0, 0, 1, false);
    cv_arc(4, 2, 1.9f, 0, 100, false);
    cv_fill();
    CHECK(pixel(4, 2) == white && pixel(0, 0) == bg);

    // the path is full at cv_max_points, and what's in it still fills
    reset();
    cv_begin_path();
    CHECK(cv_move_to(0, 0));
    for (int i = 1; i < cv_max_points; i++) {
        CHECK(cv_line_to(i % 2 == 0 ? 0.0f : 8.0f, (float)(i % 3)));
    }
    CHECK(!cv_line_to(1, 1));
    CHECK(!cv_move_to(1, 1));
    cv_fill();
    // and at cv_max_subpaths
    cv_begin_path();
    for (int i = 0; i < cv_max_subpaths; i++) {
        CHECK(cv_rounded_rect(0, 0, 1, 1, 0));
    }
    CHECK(!cv_rounded_rect(0, 0, 1, 1, 0));
    // replacing a lone move needs no room
    cv_begin_path();
    CHECK(cv_move_to(0, 0));
    for (int i = 0; i < 100000; i++) {
        CHECK(cv_move_to(1, 1));
    }
}

// A PNG of `width` × `height` pixels of `rgba`, from the stb writer
static unsigned char *png(const int width, const int height, const uint32_t rgba, int *const size) {
    unsigned char *const raw = malloc((size_t)width * height * 4);
    CHECK(raw != NULL);
    for (int i = 0; i < width * height; i++) {
        raw[i * 4 + 0] = (unsigned char)(rgba >> 24);
        raw[i * 4 + 1] = (unsigned char)(rgba >> 16);
        raw[i * 4 + 2] = (unsigned char)(rgba >> 8);
        raw[i * 4 + 3] = (unsigned char)rgba;
    }
    unsigned char *const out = stbi_write_png_to_mem(raw, width * 4, width, height, 4, size);
    CHECK(out != NULL);
    free(raw);
    return out;
}

static void test_images(void) {
    // opaque white, drawn 1:1 and stretched: the same white
    reset();
    int size;
    unsigned char *data = png(2, 2, 0xFFFFFFFF, &size);
    const uint32_t image = cv_image_load(data, (size_t)size);
    CHECK(image != 0);
    free(data);
    CHECK(cv_draw_image(image, 0, 0, 2, 2));
    CHECK(cv_draw_image(image, 4, 1, 4, 3));
    expect(__LINE__,
           "##......\n"
           "##..####\n"
           "....####\n"
           "....####\n");

    // half transparent: premultiplied, so it blends as a half white fill does
    data = png(1, 1, 0xFFFFFF80, &size);
    const uint32_t half = cv_image_load(data, (size_t)size);
    free(data);
    CHECK(cv_draw_image(half, 2, 0, 1, 1));
    CHECK(pixel(2, 0) == over(white, 128));

    // a 1 × 1 red PNG whose first IDAT chunk is empty: stb_image used to memcpy to NULL for it
    // (undefined behaviour, which UBSan stops on; patched in extern/stb)
    static const uint8_t empty_idat[] = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
        0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1F, 0x15, 0xC4,
        0x89, 0x00, 0x00, 0x00, 0x00, 0x49, 0x44, 0x41, 0x54, 0x35, 0xAF, 0x06, 0x1E, 0x00, 0x00, 0x00,
        0x0D, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0xF8, 0xCF, 0xC0, 0xF0, 0x1F, 0x00, 0x05, 0x00,
        0x01, 0xFF, 0x89, 0x99, 0x3D, 0x1D, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42,
        0x60, 0x82,
    };
    const uint32_t red = cv_image_load(empty_idat, sizeof(empty_idat));
    CHECK(red != 0 && cv_draw_image(red, 7, 3, 1, 1));
    CHECK(pixel(7, 3) == 0x00FF0000);

    // what isn't an image
    CHECK(!cv_draw_image(0, 0, 0, 1, 1));
    CHECK(!cv_draw_image(cv_max_images + 1, 0, 0, 1, 1));
    // in range, but never loaded
    CHECK(!cv_draw_image(cv_max_images, 0, 0, 1, 1));
    // NaN draws nothing but isn't an error
    CHECK(cv_draw_image(image, NAN, 0, 1, 1));

    // not a PNG, or a PNG cut short
    static const uint8_t junk[] = "\x89PNG\r\n\x1a\n not really";
    CHECK(cv_image_load(junk, sizeof(junk)) == 0);
    data = png(4, 4, 0x11223344, &size);
    CHECK(cv_image_load(data, (size_t)size / 2) == 0);
    CHECK(cv_image_load(data, 0) == 0);
    free(data);
    // wider than cv_max_image_side
    data = png(cv_max_image_side + 1, 1, 0xFFFFFFFF, &size);
    CHECK(cv_image_load(data, (size_t)size) == 0);
    free(data);

    // the 65th image doesn't fit; shutting down frees them all (reset checks the budget is empty)
    reset();
    data = png(1, 1, 0xFFFFFFFF, &size);
    for (int i = 0; i < cv_max_images; i++) {
        CHECK(cv_image_load(data, (size_t)size) == (uint32_t)i + 1);
    }
    CHECK(cv_image_load(data, (size_t)size) == 0);
    free(data);
    reset();
}

static void test_text(void) {
    reset();
    static const uint8_t a[] = "A";
    static const uint8_t v[] = "V";
    static const uint8_t av[] = "AV";
    CHECK(cv_measure_text(a, 0) == 0.0f);
    // the size scales the width; AV is kerned closer than A and V apart
    const float wa = cv_measure_text(a, 1);
    cv_font_size(20);
    CHECK(fabsf(cv_measure_text(a, 1) - 2.0f * wa) < 1e-4f);
    CHECK(cv_measure_text(av, 2) < cv_measure_text(a, 1) + cv_measure_text(v, 1));

    // drawn on the baseline: above y, not below it
    reset();
    cv_font_size(4);
    cv_fill_text(a, 1, 0, 3);
    bool above = false;
    for (uint32_t x = 0; x < w; x++) {
        above = above || pixel(x, 2) != bg;
        CHECK(pixel(x, 3) == bg);
    }
    CHECK(above);

    // bytes that aren't UTF-8, each kind: a lone continuation, a cut sequence, an overlong one,
    // a surrogate, past U+10FFFF; each draws as U+FFFD, at least once and at most once a byte
    static const uint8_t fffd[] = "\xEF\xBF\xBD";
    const float one = cv_measure_text(fffd, 3);
    static const uint8_t bad[][4] = {{0x80}, {0xE2, 0x82}, {0xC0, 0xAF}, {0xED, 0xA0, 0x80}, {0xF4, 0x90, 0x80, 0x80}};
    static const size_t bad_size[] = {1, 2, 2, 3, 4};
    for (size_t i = 0; i < 5; i++) {
        const float wide = cv_measure_text(bad[i], bad_size[i]);
        CHECK(wide >= one && wide <= one * (float)bad_size[i] + 1e-3f);
        cv_fill_text(bad[i], bad_size[i], 0, 3);
    }

    // the atlas starts over when its table is half full (small sizes, over 2048 glyphs), and when
    // it runs out of room (big ones); it still draws after
    reset();
    static const uint8_t abc[] = "abcdefghijklmnopqrstuvwxyz";
    for (int quarters = 4; quarters <= 200; quarters++) {
        cv_font_size((float)quarters / 4.0f);
        cv_fill_text(abc, 26, -100, 3);
    }
    for (int quarters = 400; quarters <= 480; quarters++) {
        cv_font_size((float)quarters / 4.0f);
        cv_fill_text(abc, 26, -100, 3);
    }
    reset();
    cv_font_size(8);
    cv_fill_text(a, 1, 0, 4);
    bool drawn = false;
    for (size_t i = 0; i < w * h; i++) {
        drawn = drawn || pixels[i] != bg;
    }
    CHECK(drawn);
}

// Floats straight from a guest: none may write outside the canvas, or hit undefined behaviour
// (UBSan, in the asan preset). A NaN or infinite argument makes the call do nothing.
static void test_wild_floats(void) {
    const float inf = INFINITY;
    const float nan = NAN;
    const struct {
        int line;
        float x, y, width, height;
        const char *map;
    } rects[] = {
        {__LINE__, nan, 0, 4, 4, nothing},
        {__LINE__, 0, 0, nan, 4, nothing},
        {__LINE__, 0, 0, inf, inf, nothing},
        {__LINE__, -inf, 0, 4, 4, nothing},
        {__LINE__, -1e30f, -1e30f, 2e30f, 2e30f, all},
        {__LINE__, -FLT_MAX, -FLT_MAX, FLT_MAX, FLT_MAX, nothing}, // ends at 0
        {__LINE__, FLT_MAX, FLT_MAX, -FLT_MAX, -FLT_MAX, all}, // 0 to FLT_MAX: the canvas and more
        {__LINE__, 1e30f, 1e30f, 4, 4, nothing},
        {__LINE__, -100, 3.5f, 1e9f, 1, "........\n........\n........\n++++++++\n"},
    };
    for (size_t i = 0; i < sizeof(rects) / sizeof(rects[0]); i++) {
        reset();
        cv_fill_rect(rects[i].x, rects[i].y, rects[i].width, rects[i].height);
        expect(rects[i].line, rects[i].map);
    }

    // everything else, with every one of these in every argument
    static const float wild[] = {NAN, INFINITY, -INFINITY, FLT_MAX, -FLT_MAX, 1e30f, -1e30f, 1e-30f, 0.0f, -0.0f, 3.5f};
    static const uint8_t text[] = "Wild";
    int size;
    unsigned char *const data = png(3, 2, 0xFF00FFFF, &size);
    for (size_t i = 0; i < sizeof(wild) / sizeof(wild[0]); i++) {
        for (size_t j = 0; j < sizeof(wild) / sizeof(wild[0]); j++) {
            const float a = wild[i];
            const float b = wild[j];
            reset();
            const uint32_t image = cv_image_load(data, (size_t)size);
            CHECK(cv_save());
            cv_translate(a, b);
            cv_scale(b, a);
            cv_scale(a, a);
            cv_line_width(a);
            cv_font_size(b);
            cv_begin_path();
            cv_move_to(a, b);
            cv_line_to(b, a);
            CHECK(cv_arc(a, b, fabsf(a), a, b, i % 2 == 0));
            CHECK(cv_arc(1, 1, 3, a, b, j % 2 == 0));
            CHECK(cv_rounded_rect(a, b, a, b, b));
            cv_line_to(4, 4);
            cv_close_path();
            cv_fill();
            cv_stroke();
            cv_fill_rect(a, b, b, a);
            CHECK(cv_draw_image(image, a, b, a, b));
            cv_fill_text(text, 4, a, b);
            (void)cv_measure_text(text, 4);
            cv_clip_rect(a, b, a, b);
            cv_fill_rect(0, 0, w, h);
            cv_restore();
            check_guards();
        }
    }
    free(data);
}

int main(void) {
    const mem_budget_def_t def = {"canvas", 8 * 1024 * 1024, mem_kind_heap};
    CHECK(mem_init(&mem, &def, &budget, 1) == pl_result_ok);
    test_coverage();
    test_nonzero();
    test_colour();
    test_state();
    test_paths();
    test_images();
    test_text();
    test_wild_floats();
    cv_shutdown(&ctx);
    CHECK(mem_shutdown(&mem) == 0);
    return 0;
}
