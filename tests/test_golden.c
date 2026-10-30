// SPDX-License-Identifier: MIT OR GPL-2.0-only

// One scene per canvas primitive, compared with tests/golden/<name>.png (golden.h says how)

#include "golden.h"
#include "mim_test.h"
#include "runtime/canvas/canvas.h"

#include <math.h>
#include <stdbool.h>
#include <string.h>

enum {
    w = 320,
    h = 180,
};

static const float pi = 3.14159265f;

static mem_t mem;
static mem_budget_t budget;
static uint32_t pixels[w * h];
static cv_context_t ctx;

static void text(const char *const s, const float x, const float y) {
    cv_fill_text((const uint8_t *)s, strlen(s), x, y);
}

static float measure(const char *const s) {
    return cv_measure_text((const uint8_t *)s, strlen(s));
}

// A light background with a faint grid every 10 pixels, so anti-aliasing and blending show
static void background(void) {
    cv_fill_style(0xFFF4F1EA);
    cv_fill_rect(0, 0, w, h);
    cv_fill_style(0xFFE2DDD2);
    for (int i = 0; i <= w; i += 10) {
        cv_fill_rect((float)i, 0, 1, h);
    }
    for (int i = 0; i <= h; i += 10) {
        cv_fill_rect(0, (float)i, w, 1);
    }
}

static void polygon(const float *const xy, const int count) {
    cv_begin_path();
    cv_move_to(xy[0], xy[1]);
    for (int i = 1; i < count; i++) {
        cv_line_to(xy[i * 2], xy[i * 2 + 1]);
    }
    cv_close_path();
}

static void scene_fill(void) {
    // rectangles on and off the pixel grid, and translucent ones overlapping
    cv_fill_style(0xFF1F6FEB);
    cv_fill_rect(10, 10, 40, 30);
    cv_fill_rect(60.5f, 10.5f, 40, 30);
    cv_fill_rect(110.25f, 10.75f, 39.5f, 29.5f);
    cv_fill_style(0x80E5534B);
    cv_fill_rect(20, 25, 120, 30);
    // a triangle, a concave arrow, and a five-point star drawn in one go: by the non-zero rule its
    // middle, crossed twice, is filled
    cv_fill_style(0xFF2DA44E);
    polygon((const float[]){170, 50, 200, 10, 230, 50}, 3);
    cv_fill();
    polygon((const float[]){240, 20, 280, 20, 280, 10, 305, 30, 280, 50, 280, 40, 240, 40}, 7);
    cv_fill();
    cv_fill_style(0xFFBF8700);
    float star[10];
    for (int i = 0; i < 5; i++) {
        const float a = -pi / 2 + (float)i * 4.0f * pi / 5.0f;
        star[i * 2] = 60 + 45 * cosf(a);
        star[i * 2 + 1] = 120 + 45 * sinf(a);
    }
    polygon(star, 5);
    cv_fill();
    // a square with a square hole: the inner one goes the other way round
    cv_fill_style(0xFF8250DF);
    cv_begin_path();
    cv_move_to(130, 80);
    cv_line_to(210, 80);
    cv_line_to(210, 160);
    cv_line_to(130, 160);
    cv_move_to(150, 100);
    cv_line_to(150, 140);
    cv_line_to(190, 140);
    cv_line_to(190, 100);
    cv_fill();
    // thin slivers: anti-aliasing at a fraction of a pixel
    cv_fill_style(0xFF24292F);
    for (int i = 0; i < 8; i++) {
        cv_fill_rect(230 + (float)i * 10, 80, 0.125f * (float)(i + 1), 80);
    }
}

static void scene_arcs(void) {
    cv_fill_style(0xFF1F6FEB);
    const float radii[] = {0.5f, 1, 2, 4, 8, 16, 32};
    float x = 10;
    for (int i = 0; i < 7; i++) {
        cv_begin_path();
        cv_arc(x + radii[i], 40, radii[i], 0, 2 * pi, false);
        cv_fill();
        x += 2 * radii[i] + 8;
    }
    // pie slices: clockwise and counterclockwise from the same angles
    cv_fill_style(0xFFE5534B);
    cv_begin_path();
    cv_move_to(60, 130);
    cv_arc(60, 130, 40, 0, pi / 3, false);
    cv_close_path();
    cv_fill();
    cv_fill_style(0xFF2DA44E);
    cv_begin_path();
    cv_move_to(160, 130);
    cv_arc(160, 130, 40, 0, pi / 3, true);
    cv_close_path();
    cv_fill();
    // a ring: two circles, the inner one the other way round
    cv_fill_style(0xFF8250DF);
    cv_begin_path();
    cv_arc(260, 130, 40, 0, 2 * pi, false);
    cv_arc(260, 130, 25, 2 * pi, 0, true);
    cv_fill();
    // a circle much bigger than the canvas, its edge cutting across the top right corner
    cv_fill_style(0x60BF8700);
    cv_begin_path();
    cv_arc(3320, -3000, 4280, 0, 2 * pi, false);
    cv_fill();
}

static void scene_rounded(void) {
    const float radii[] = {0, 2, 6, 12, 20, 100};
    const uint32_t colors[] = {0xFF1F6FEB, 0xFF2DA44E, 0xFFBF8700, 0xFFE5534B, 0xFF8250DF, 0xFF24292F};
    for (int i = 0; i < 6; i++) {
        cv_fill_style(colors[i]);
        cv_begin_path();
        cv_rounded_rect(10 + (float)i * 52, 20, 44, 60, radii[i]);
        cv_fill();
    }
    // a pill, off the grid, translucent over a stripe
    cv_fill_style(0xFF24292F);
    cv_fill_rect(0, 120, w, 10);
    cv_fill_style(0xA0E5534B);
    cv_begin_path();
    cv_rounded_rect(20.5f, 100.25f, 280, 50, 25);
    cv_fill();
    // a negative size goes the other way, as in HTML
    cv_fill_style(0xFF2DA44E);
    cv_begin_path();
    cv_rounded_rect(310, 175, -30, -15, 4);
    cv_fill();
}

static void scene_stroke(void) {
    // widths 1 to 10, on the grid and between pixels
    cv_stroke_style(0xFF24292F);
    const float widths[] = {1, 1, 2, 3, 5, 10};
    for (int i = 0; i < 6; i++) {
        cv_line_width(widths[i]);
        cv_begin_path();
        const float y = 15 + (float)i * 14 + (i == 0 ? 0.5f : 0.0f);
        cv_move_to(10, y);
        cv_line_to(100, y);
        cv_stroke();
    }
    // slanted lines
    cv_stroke_style(0xFF1F6FEB);
    for (int i = 0; i < 6; i++) {
        cv_line_width((float)(i + 1));
        cv_begin_path();
        cv_move_to(115 + (float)i * 14, 10);
        cv_line_to(125 + (float)i * 14, 90);
        cv_stroke();
    }
    // corners: mitred, and at (270, 30), where the line almost turns back, bevelled past the
    // miter limit
    cv_stroke_style(0xFFE5534B);
    cv_line_width(8);
    cv_begin_path();
    cv_move_to(210, 80);
    cv_line_to(230, 20);
    cv_line_to(250, 80);
    cv_line_to(270, 30);
    cv_line_to(254, 80);
    cv_line_to(310, 75);
    cv_stroke();
    // closed shapes have corners all round
    cv_stroke_style(0xFF2DA44E);
    cv_line_width(6);
    polygon((const float[]){20, 170, 60, 110, 100, 170}, 3);
    cv_stroke();
    cv_stroke_style(0xFF8250DF);
    cv_line_width(3);
    cv_begin_path();
    cv_rounded_rect(120, 110, 80, 55, 14);
    cv_stroke();
    // an arc, open at both ends
    cv_stroke_style(0xC0BF8700);
    cv_line_width(12);
    cv_begin_path();
    cv_arc(260, 140, 30, pi, 2 * pi, false);
    cv_stroke();
}

static void scene_state(void) {
    // the same square, translated and scaled by save/restore
    for (int i = 0; i < 5; i++) {
        CHECK(cv_save());
        cv_translate(15 + (float)i * 40, 15);
        cv_scale(1 + (float)i * 0.25f, 1 + (float)i * 0.5f);
        cv_fill_style(0xFF1F6FEB);
        cv_fill_rect(0, 0, 20, 20);
        cv_stroke_style(0xFF24292F);
        cv_begin_path();
        cv_rounded_rect(0, 0, 20, 20, 5);
        cv_stroke();
        cv_restore();
    }
    // mirrored: a triangle pointing right, flipped to point left
    for (int i = 0; i < 2; i++) {
        CHECK(cv_save());
        cv_translate(270, 30);
        cv_scale(i == 0 ? 1.0f : -1.0f, 1);
        cv_fill_style(i == 0 ? 0xFF2DA44E : 0xFFE5534B);
        polygon((const float[]){10, -20, 50, 0, 10, 20}, 3);
        cv_fill();
        cv_restore();
    }
    // a clip, a narrower one inside it, and what's drawn over both
    CHECK(cv_save());
    cv_clip_rect(20, 100, 180, 70);
    cv_fill_style(0xFFE2DDD2);
    cv_fill_rect(0, 0, w, h);
    CHECK(cv_save());
    cv_clip_rect(60.4f, 90, 60.2f, 100);
    cv_fill_style(0xFF8250DF);
    cv_begin_path();
    cv_arc(90, 135, 50, 0, 2 * pi, false);
    cv_fill();
    cv_restore();
    cv_fill_style(0x80BF8700);
    cv_begin_path();
    cv_arc(180, 135, 45, 0, 2 * pi, false);
    cv_fill();
    cv_restore();
    // after restore, nothing is clipped
    cv_fill_style(0xFF24292F);
    cv_fill_rect(230, 120, 80, 4);
}

// A 16 × 12 test card: hue across, fading out to the right, with a dark frame
static uint32_t card(void) {
    unsigned char raw[16 * 12 * 4];
    for (int y = 0; y < 12; y++) {
        for (int x = 0; x < 16; x++) {
            unsigned char *const p = raw + (y * 16 + x) * 4;
            const bool frame = x == 0 || y == 0 || x == 15 || y == 11;
            p[0] = frame ? 30 : (unsigned char)(x * 16);
            p[1] = frame ? 30 : (unsigned char)(y * 21);
            p[2] = frame ? 30 : (unsigned char)(255 - x * 16);
            p[3] = frame ? 255 : (unsigned char)(255 - x * 10);
        }
    }
    int size;
    unsigned char *const data = stbi_write_png_to_mem(raw, 16 * 4, 16, 12, 4, &size);
    CHECK(data != NULL);
    const uint32_t image = cv_image_load(data, (size_t)size);
    STBIW_FREE(data);
    return image;
}

static void scene_images(void) {
    const uint32_t image = card();
    CHECK(image != 0);
    // 1:1, then 4 times, then smaller than itself
    CHECK(cv_draw_image(image, 10, 10, 16, 12));
    CHECK(cv_draw_image(image, 40, 10, 64, 48));
    CHECK(cv_draw_image(image, 115, 10, 8, 6));
    CHECK(cv_draw_image(image, 130.5f, 10.5f, 33, 21));
    // over a striped background, so the fade shows
    for (int i = 0; i < 8; i++) {
        cv_fill_style(i % 2 == 0 ? 0xFF24292F : 0xFFF4F1EA);
        cv_fill_rect(180 + (float)i * 16, 10, 16, 60);
    }
    CHECK(cv_draw_image(image, 180, 10, 128, 60));
    // mirrored by the transform, and clipped
    CHECK(cv_save());
    cv_translate(150, 90);
    cv_scale(-1, 1);
    CHECK(cv_draw_image(image, 0, 0, 128, 80));
    cv_restore();
    CHECK(cv_save());
    cv_clip_rect(180, 100, 60, 50);
    CHECK(cv_draw_image(image, 170, 90, 128, 80));
    cv_restore();
}

static void scene_text(void) {
    // sizes, with kerned pairs
    cv_fill_style(0xFF24292F);
    const float sizes[] = {8, 10, 12, 16};
    const float baselines[] = {14, 28, 44, 64};
    for (int i = 0; i < 4; i++) {
        cv_font_size(sizes[i]);
        text("MIM draws AV, Wa, To.", 8, baselines[i]);
    }
    // centred on the canvas by its measured width, underlined to show the width, and not ASCII
    cv_font_size(20);
    cv_fill_style(0xFF1F6FEB);
    const char *const cafe = "Caf\xC3\xA9 d\xC3\xA9j\xC3\xA0 vu \xE2\x80\x94 \xC3\x85ngstr\xC3\xB6m";
    const float width = measure(cafe);
    text(cafe, 160 - width / 2, 94);
    cv_fill_style(0xFFE5534B);
    cv_fill_rect(160 - width / 2, 98, width, 1);
    // big
    cv_font_size(40);
    cv_fill_style(0xFF24292F);
    text("AV Wa", 8, 145);
    // bytes that aren't UTF-8 draw as U+FFFD, which this font shows as a box
    cv_font_size(10);
    text("bad: \xFF\xC0\xAF.", 8, 172);
    // scaled by the transform and translucent; then clipped
    CHECK(cv_save());
    cv_translate(200, 112);
    cv_scale(2, 2);
    cv_font_size(12);
    cv_fill_style(0xA02DA44E);
    text("Scaled", 0, 12);
    cv_restore();
    CHECK(cv_save());
    cv_clip_rect(200, 145, 100, 25);
    cv_font_size(32);
    cv_fill_style(0xFF8250DF);
    text("Clipped", 196, 172);
    cv_restore();
}

static const struct {
    const char *name;
    void (*draw)(void);
} scenes[] = {
    {"fill", scene_fill},
    {"arcs", scene_arcs},
    {"rounded", scene_rounded},
    {"stroke", scene_stroke},
    {"state", scene_state},
    {"images", scene_images},
    {"text", scene_text},
};

int main(void) {
    const mem_budget_def_t def = {"canvas", 16 * 1024 * 1024, mem_kind_heap};
    CHECK(mem_init(&mem, &def, &budget, 1) == pl_result_ok);
    bool ok = true;
    for (size_t i = 0; i < sizeof(scenes) / sizeof(scenes[0]); i++) {
        cv_init(&ctx, pixels, w, h, &budget);
        cv_set_context(&ctx);
        background();
        scenes[i].draw();
        cv_shutdown(&ctx);
        CHECK(mem_get_stats(&budget).used_blocks == 0);
        // every scene, even after one fails
        ok = golden_check(scenes[i].name, pixels, w, h) && ok;
    }
    CHECK(mem_shutdown(&mem) == 0);
    return ok ? 0 : 1;
}
