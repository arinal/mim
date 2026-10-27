// SPDX-License-Identifier: MIT OR GPL-2.0-only

//! A white box bouncing left and right on dark blue; never quits

#![no_std]

use mim::{Level, SCREEN_HEIGHT as H, SCREEN_WIDTH as W, Tick, log};

const BOX: usize = 64;
const TOP: usize = (H - BOX) / 2;
const BLUE: u32 = 0x00_20_20_40;
const WHITE: u32 = 0x00_FF_FF_FF;

// 3.6 MB, in linear memory from the start
static mut PIXELS: [u32; W * H] = [0; W * H];

/// The box's left edge: 1 px per 4 ms, from 0 to W - BOX and back
fn box_x(abs_time_ms: u64) -> usize {
    let span = (W - BOX) as u64;
    let p = (abs_time_ms / 4) % (2 * span);
    (if p <= span { p } else { 2 * span - p }) as usize
}

#[unsafe(no_mangle)]
pub extern "C" fn app_init() {
    log(Level::Info, "bounce: close the window or press Ctrl-C to stop");
}

#[unsafe(no_mangle)]
pub extern "C" fn app_tick(abs_time_ms: u64, _dt_seconds: f32) -> Tick {
    // the only reference to PIXELS, and the host calls in on one thread
    let pixels = unsafe { &mut *(&raw mut PIXELS) };
    pixels.fill(BLUE);
    let x = box_x(abs_time_ms);
    for row in pixels[TOP * W..(TOP + BOX) * W].chunks_exact_mut(W) {
        row[x..x + BOX].fill(WHITE);
    }
    mim::present(pixels);
    Tick::Continue
}

#[unsafe(no_mangle)]
pub extern "C" fn app_shutdown() {}
