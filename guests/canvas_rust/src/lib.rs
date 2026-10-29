// SPDX-License-Identifier: MIT OR GPL-2.0-only

//! A TV-style home screen that uses every canvas call; guests/canvas_cpp draws exactly the same
//! frame, pixel for pixel. Never quits.

#![no_std]

use mim::cv::{self, Image};
use mim::{Level, SCREEN_HEIGHT, SCREEN_WIDTH, Tick, log};

const W: f32 = SCREEN_WIDTH as f32;
const H: f32 = SCREEN_HEIGHT as f32;
const PI: f32 = 3.14159265;

const TILE_W: f32 = 216.0;
const TILE_H: f32 = 122.0;
const TILE_GAP: f32 = 24.0;
const ROW_Y: f32 = 220.0;
const FOCUSED: usize = 1;
const TITLES: [&str; 5] = ["Mountains", "Sunset", "Ridge", "Dusk", "Peaks"];
// how far each has been watched, out of 1
const WATCHED: [f32; 5] = [0.8, 0.35, 0.6, 0.1, 0.95];
// the star badge's points, alternating 18 and 7.5 from its centre
const STAR: [f32; 20] = [
    0.000, -18.000, 4.408, -6.068, 17.119, -5.562, 7.133, 2.318, 10.580, 14.562, 0.000, 7.500,
    -10.580, 14.562, -7.133, 2.318, -17.119, -5.562, -4.408, -6.068,
];

const BACKGROUND: u32 = 0xFF0E1117;
const WHITE: u32 = 0xFFFFFFFF;
const GREY: u32 = 0xFF8B949E;
const BLUE: u32 = 0xFF2F81F7;

static mut POSTER: Option<Image> = None;

fn tile_x(i: usize) -> f32 {
    64.0 + i as f32 * (TILE_W + TILE_GAP)
}

/// Covers the corners outside a rounded rectangle: the rectangle anticlockwise, the rounded one
/// clockwise, so by the non-zero rule only the corners are left
fn round_corners(x: f32, y: f32, w: f32, h: f32, r: f32) {
    cv::begin_path();
    cv::move_to(x, y);
    cv::line_to(x, y + h);
    cv::line_to(x + w, y + h);
    cv::line_to(x + w, y);
    cv::close_path();
    cv::rounded_rect(x, y, w, h, r);
    cv::fill_style(BACKGROUND);
    cv::fill();
}

fn tile(i: usize, poster: Image) {
    let x = tile_x(i);
    cv::save();
    if i == FOCUSED {
        // grown by 6 % around its centre
        cv::translate(x + TILE_W / 2.0, ROW_Y + TILE_H / 2.0);
        cv::scale(1.06, 1.06);
        cv::translate(-(x + TILE_W / 2.0), -(ROW_Y + TILE_H / 2.0));
    }
    cv::save();
    cv::clip_rect(x, ROW_Y, TILE_W, TILE_H);
    poster.draw(x, ROW_Y, TILE_W, TILE_H);
    cv::fill_style(0xA0000000);
    cv::fill_rect(x, ROW_Y + TILE_H - 8.0, TILE_W, 8.0);
    cv::fill_style(BLUE);
    cv::fill_rect(x, ROW_Y + TILE_H - 8.0, TILE_W * WATCHED[i], 8.0);
    cv::restore();
    round_corners(x, ROW_Y, TILE_W, TILE_H, 12.0);
    if i == FOCUSED {
        cv::stroke_style(WHITE);
        cv::line_width(4.0);
        cv::begin_path();
        cv::rounded_rect(x - 6.0, ROW_Y - 6.0, TILE_W + 12.0, TILE_H + 12.0, 16.0);
        cv::stroke();
    }
    cv::restore();
    cv::font_size(18.0);
    cv::fill_style(if i == FOCUSED { WHITE } else { GREY });
    cv::fill_text(TITLES[i], x, ROW_Y + TILE_H + 32.0);
}

fn star(cx: f32, cy: f32) {
    cv::begin_path();
    cv::move_to(cx + STAR[0], cy + STAR[1]);
    for p in STAR[2..].chunks_exact(2) {
        cv::line_to(cx + p[0], cy + p[1]);
    }
    cv::close_path();
    cv::fill_style(0xFFE3B341);
    cv::fill();
}

#[unsafe(no_mangle)]
pub extern "C" fn app_init() {
    let poster = Image::load(include_bytes!("../../assets/poster.png"));
    if poster.is_none() {
        log(Level::Error, "canvas: the poster didn't load");
    }
    // the only access to POSTER outside app_tick, and the host calls in on one thread
    unsafe { POSTER = poster };
}

#[unsafe(no_mangle)]
pub extern "C" fn app_tick(abs_time_ms: u64, _dt_seconds: f32) -> Tick {
    let Some(poster) = (unsafe { POSTER }) else {
        return Tick::Quit;
    };
    cv::fill_style(BACKGROUND);
    cv::fill_rect(0.0, 0.0, W, H);

    // the header: a title, a dimmer word after it, and a label right-aligned
    cv::font_size(40.0);
    cv::fill_style(WHITE);
    cv::fill_text("MIM", 64.0, 92.0);
    let title = cv::measure_text("MIM");
    cv::font_size(20.0);
    cv::fill_style(GREY);
    cv::fill_text("isn't M5", 64.0 + title + 16.0, 92.0);
    let label = "4b \u{00B7} canvas";
    cv::fill_text(label, W - 64.0 - cv::measure_text(label), 92.0);
    cv::fill_style(0xFF30363D);
    cv::fill_rect(64.0, 116.0, W - 128.0, 1.0);

    cv::font_size(24.0);
    cv::fill_style(WHITE);
    cv::fill_text("Continue watching", 64.0, 190.0);
    star(64.0 + cv::measure_text("Continue watching") + 30.0, 181.0);
    for i in 0..TITLES.len() {
        tile(i, poster);
    }

    // a spinner, turning with the time
    let a = abs_time_ms as f32 * 0.004;
    cv::stroke_style(BLUE);
    cv::line_width(6.0);
    cv::begin_path();
    cv::arc(W - 100.0, 520.0, 28.0, a, a + 1.5 * PI, false);
    cv::stroke();
    cv::begin_path();
    cv::arc(W - 100.0, 520.0, 10.0, 0.0, 2.0 * PI, false);
    cv::fill_style(GREY);
    cv::fill();

    // a translucent bar along the bottom, its text centred
    cv::fill_style(0xC0000000);
    cv::fill_rect(0.0, H - 80.0, W, 80.0);
    cv::font_size(22.0);
    cv::fill_style(WHITE);
    let hint = "Press OK to play";
    cv::fill_text(hint, (W - cv::measure_text(hint)) / 2.0, H - 32.0);
    Tick::Continue
}

#[unsafe(no_mangle)]
pub extern "C" fn app_shutdown() {}
