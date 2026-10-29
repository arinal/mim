//===-- canvas.cpp - The canvas demo, frame for frame as canvas_rust -------===//
//
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// Every float here is a float, literals included (1.5f, not 1.5): a double
// in the middle would round differently from the Rust guest.
//
//===----------------------------------------------------------------------===//

#include "mim.h"

// the poster as a C array, generated from guests/assets/poster.png
#include "poster_png.h"

namespace {

using mim::cv::Image;

constexpr float W = float(mim::screen_width);
constexpr float H = float(mim::screen_height);
constexpr float PI = 3.14159265f;

constexpr float TILE_W = 216.0f;
constexpr float TILE_H = 122.0f;
constexpr float TILE_GAP = 24.0f;
constexpr float ROW_Y = 220.0f;
constexpr uint32_t FOCUSED = 1;
constexpr uint32_t TILES = 5;
const char *const TITLES[TILES] = {"Mountains", "Sunset", "Ridge", "Dusk", "Peaks"};
// how far each has been watched, out of 1
constexpr float WATCHED[TILES] = {0.8f, 0.35f, 0.6f, 0.1f, 0.95f};
// the star badge's points, alternating 18 and 7.5 from its centre
constexpr float STAR[20] = {
    0.000f, -18.000f, 4.408f, -6.068f, 17.119f, -5.562f, 7.133f, 2.318f, 10.580f, 14.562f, 0.000f, 7.500f,
    -10.580f, 14.562f, -7.133f, 2.318f, -17.119f, -5.562f, -4.408f, -6.068f,
};

constexpr uint32_t BACKGROUND = 0xFF0E1117;
constexpr uint32_t WHITE = 0xFFFFFFFF;
constexpr uint32_t GREY = 0xFF8B949E;
constexpr uint32_t BLUE = 0xFF2F81F7;

Image poster;

float tile_x(uint32_t i) {
    return 64.0f + float(i) * (TILE_W + TILE_GAP);
}

// Covers the corners outside a rounded rectangle: the rectangle anticlockwise, the rounded one
// clockwise, so by the non-zero rule only the corners are left
void round_corners(float x, float y, float w, float h, float r) {
    mim::cv::begin_path();
    mim::cv::move_to(x, y);
    mim::cv::line_to(x, y + h);
    mim::cv::line_to(x + w, y + h);
    mim::cv::line_to(x + w, y);
    mim::cv::close_path();
    mim::cv::rounded_rect(x, y, w, h, r);
    mim::cv::fill_style(BACKGROUND);
    mim::cv::fill();
}

void tile(uint32_t i) {
    const float x = tile_x(i);
    mim::cv::save();
    if (i == FOCUSED) {
        // grown by 6 % around its centre
        mim::cv::translate(x + TILE_W / 2.0f, ROW_Y + TILE_H / 2.0f);
        mim::cv::scale(1.06f, 1.06f);
        mim::cv::translate(-(x + TILE_W / 2.0f), -(ROW_Y + TILE_H / 2.0f));
    }
    mim::cv::save();
    mim::cv::clip_rect(x, ROW_Y, TILE_W, TILE_H);
    poster.draw(x, ROW_Y, TILE_W, TILE_H);
    mim::cv::fill_style(0xA0000000);
    mim::cv::fill_rect(x, ROW_Y + TILE_H - 8.0f, TILE_W, 8.0f);
    mim::cv::fill_style(BLUE);
    mim::cv::fill_rect(x, ROW_Y + TILE_H - 8.0f, TILE_W * WATCHED[i], 8.0f);
    mim::cv::restore();
    round_corners(x, ROW_Y, TILE_W, TILE_H, 12.0f);
    if (i == FOCUSED) {
        mim::cv::stroke_style(WHITE);
        mim::cv::line_width(4.0f);
        mim::cv::begin_path();
        mim::cv::rounded_rect(x - 6.0f, ROW_Y - 6.0f, TILE_W + 12.0f, TILE_H + 12.0f, 16.0f);
        mim::cv::stroke();
    }
    mim::cv::restore();
    mim::cv::font_size(18.0f);
    mim::cv::fill_style(i == FOCUSED ? WHITE : GREY);
    mim::cv::fill_text(TITLES[i], x, ROW_Y + TILE_H + 32.0f);
}

void star(float cx, float cy) {
    mim::cv::begin_path();
    mim::cv::move_to(cx + STAR[0], cy + STAR[1]);
    for (uint32_t p = 2; p < 20; p += 2) {
        mim::cv::line_to(cx + STAR[p], cy + STAR[p + 1]);
    }
    mim::cv::close_path();
    mim::cv::fill_style(0xFFE3B341);
    mim::cv::fill();
}

} // namespace

extern "C" {

void app_init() {
    poster = Image::load(poster_png, poster_png_size);
    if (!poster.ok()) {
        mim::log(mim::Level::error, "canvas: the poster didn't load");
    }
}

int32_t app_tick(uint64_t abs_time_ms, float dt_seconds) {
    (void)dt_seconds;
    if (!poster.ok()) {
        return int32_t(mim::Tick::quit);
    }
    mim::cv::fill_style(BACKGROUND);
    mim::cv::fill_rect(0.0f, 0.0f, W, H);

    // the header: a title, a dimmer word after it, and a label right-aligned
    mim::cv::font_size(40.0f);
    mim::cv::fill_style(WHITE);
    mim::cv::fill_text("MIM", 64.0f, 92.0f);
    const float title = mim::cv::measure_text("MIM");
    mim::cv::font_size(20.0f);
    mim::cv::fill_style(GREY);
    mim::cv::fill_text("isn't M5", 64.0f + title + 16.0f, 92.0f);
    const char *const label = "4b \xC2\xB7 canvas";
    mim::cv::fill_text(label, W - 64.0f - mim::cv::measure_text(label), 92.0f);
    mim::cv::fill_style(0xFF30363D);
    mim::cv::fill_rect(64.0f, 116.0f, W - 128.0f, 1.0f);

    mim::cv::font_size(24.0f);
    mim::cv::fill_style(WHITE);
    mim::cv::fill_text("Continue watching", 64.0f, 190.0f);
    star(64.0f + mim::cv::measure_text("Continue watching") + 30.0f, 181.0f);
    for (uint32_t i = 0; i < TILES; i++) {
        tile(i);
    }

    // a spinner, turning with the time
    const float a = float(abs_time_ms) * 0.004f;
    mim::cv::stroke_style(BLUE);
    mim::cv::line_width(6.0f);
    mim::cv::begin_path();
    mim::cv::arc(W - 100.0f, 520.0f, 28.0f, a, a + 1.5f * PI);
    mim::cv::stroke();
    mim::cv::begin_path();
    mim::cv::arc(W - 100.0f, 520.0f, 10.0f, 0.0f, 2.0f * PI);
    mim::cv::fill_style(GREY);
    mim::cv::fill();

    // a translucent bar along the bottom, its text centred
    mim::cv::fill_style(0xC0000000);
    mim::cv::fill_rect(0.0f, H - 80.0f, W, 80.0f);
    mim::cv::font_size(22.0f);
    mim::cv::fill_style(WHITE);
    const char *const hint = "Press OK to play";
    mim::cv::fill_text(hint, (W - mim::cv::measure_text(hint)) / 2.0f, H - 32.0f);
    return int32_t(mim::Tick::keep_going);
}

void app_shutdown() {}
}
