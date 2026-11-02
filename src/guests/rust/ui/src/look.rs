// SPDX-License-Identifier: MIT OR GPL-2.0-only

//! What every screen shares: the logical screen, the colours, the names, and the pieces drawn on
//! more than one screen

use mim::{SCREEN_WIDTH, cv};

/// Everything is laid out on this screen; one scale fits it to the real one
pub const W: f32 = 1920.0;
pub const H: f32 = 1080.0;
pub const SCALE: f32 = SCREEN_WIDTH as f32 / W;

pub const LEFT: f32 = 96.0;
pub const HEADER_H: f32 = 160.0;

pub const BACKGROUND: u32 = 0xFF0E1117;
pub const WHITE: u32 = 0xFFFFFFFF;
pub const GREY: u32 = 0xFF8B949E;
const LINE: u32 = 0xFF30363D;
const PALETTE: [u32; 8] = [
    0xFF1F6FEB, 0xFF8957E5, 0xFFBF4B8A, 0xFFD1242F, 0xFFBC4C00, 0xFF9A6700, 0xFF1A7F37, 0xFF0E7490,
];
const NAMES: [&str; 16] = [
    "Mountains",
    "Sunset",
    "Ridge",
    "Dusk",
    "Peaks",
    "Harbour",
    "Glacier",
    "Canyon",
    "Lagoon",
    "Tundra",
    "Meadow",
    "Delta",
    "Summit",
    "Reef",
    "Dune",
    "Fjord",
];
pub const ROW_TITLES: [&str; 4] = ["Continue watching", "Trending now", "Movies", "Series"];

pub fn color(row: usize, col: usize) -> u32 {
    PALETTE[(row * 3 + col) % 8]
}

pub fn title(row: usize, col: usize) -> &'static str {
    NAMES[(row * 7 + col * 3) % 16]
}

pub fn background() {
    cv::fill_style(BACKGROUND);
    cv::fill_rect(0.0, 0.0, W, H);
}

/// The bar along the top, over anything scrolled under it, with `label` on the right
pub fn header(label: &str) {
    cv::fill_style(BACKGROUND);
    cv::fill_rect(0.0, 0.0, W, HEADER_H);
    cv::font_size(56.0);
    cv::fill_style(WHITE);
    cv::fill_text("MIM", LEFT, 104.0);
    let mim = cv::measure_text("MIM");
    cv::font_size(28.0);
    cv::fill_style(GREY);
    cv::fill_text("isn't M5", LEFT + mim + 24.0, 104.0);
    cv::fill_text(label, W - LEFT - cv::measure_text(label), 104.0);
    cv::fill_style(LINE);
    cv::fill_rect(LEFT, 138.0, W - 2.0 * LEFT, 2.0);
}

/// A translucent bar along the bottom, `text` centred in it
pub fn hint(text: &str) {
    cv::fill_style(0xC0000000);
    cv::fill_rect(0.0, H - 96.0, W, 96.0);
    cv::font_size(28.0);
    cv::fill_style(WHITE);
    cv::fill_text(text, (W - cv::measure_text(text)) / 2.0, H - 38.0);
}

/// A rounded tile with its title, grown by `zoom` around its centre, ringed if focused
pub fn tile(x: f32, y: f32, w: f32, h: f32, color: u32, title: &str, zoom: f32, focused: bool) {
    cv::save();
    let cx = x + w / 2.0;
    let cy = y + h / 2.0;
    cv::translate(cx, cy);
    cv::scale(zoom, zoom);
    cv::translate(-cx, -cy);
    cv::begin_path();
    cv::rounded_rect(x, y, w, h, 16.0);
    cv::fill_style(color);
    cv::fill();
    cv::font_size(26.0);
    cv::fill_style(WHITE);
    cv::fill_text(title, x + 20.0, y + h - 22.0);
    if focused {
        cv::stroke_style(WHITE);
        cv::line_width(6.0);
        cv::begin_path();
        cv::rounded_rect(x - 9.0, y - 9.0, w + 18.0, h + 18.0, 24.0);
        cv::stroke();
    }
    cv::restore();
}
