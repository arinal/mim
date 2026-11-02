// SPDX-License-Identifier: MIT OR GPL-2.0-only

//! A white box bouncing left and right on dark blue; never quits

#![no_std]

use mim::{App, Level, SCREEN_HEIGHT as H, SCREEN_WIDTH as W, Tick, cv, log};

const BOX: usize = 64;
const TOP: usize = (H - BOX) / 2;
const BLUE: u32 = 0xFF_20_20_40;
const WHITE: u32 = 0xFF_FF_FF_FF;

/// The box's left edge: 1 px per 4 ms, from 0 to W - BOX and back
fn box_x(abs_time_ms: u64) -> usize {
    let span = (W - BOX) as u64;
    let p = (abs_time_ms / 4) % (2 * span);
    (if p <= span { p } else { 2 * span - p }) as usize
}

struct Bounce;

impl App for Bounce {
    fn init() -> Self {
        log(
            Level::Info,
            "bounce: close the window or press Ctrl-C to stop",
        );
        Bounce
    }

    fn tick(&mut self, abs_time_ms: u64, _dt_seconds: f32) -> Tick {
        // the whole screen every frame: what isn't drawn over stays from the last one
        cv::fill_style(BLUE);
        cv::fill_rect(0.0, 0.0, W as f32, H as f32);
        cv::fill_style(WHITE);
        cv::fill_rect(
            box_x(abs_time_ms) as f32,
            TOP as f32,
            BOX as f32,
            BOX as f32,
        );
        Tick::Continue
    }
}

mim::app!(Bounce);
