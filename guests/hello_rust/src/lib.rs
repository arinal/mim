// SPDX-License-Identifier: MIT OR GPL-2.0-only

//! Logs the same lines as the C++ hello guest

#![no_std]

extern crate alloc;

use alloc::vec::Vec;
use core::sync::atomic::{AtomicU32, Ordering};
use mim::{Level, Tick, log};

static TICKS: AtomicU32 = AtomicU32::new(0);

#[unsafe(no_mangle)]
pub extern "C" fn app_init() {
    log(Level::Info, "hello");
    // 400 KB: more than the guest starts with, so this grows linear memory
    let numbers: Vec<u32> = (0..100_000).collect();
    let sum: u64 = numbers.iter().map(|&n| u64::from(n)).sum();
    log!(Level::Info, "sum of 0..99999: {}", sum);
}

#[unsafe(no_mangle)]
pub extern "C" fn app_tick(abs_time_ms: u64, dt_seconds: f32) -> Tick {
    let n = TICKS.fetch_add(1, Ordering::Relaxed) + 1;
    let dt_ms = (dt_seconds * 1000.0 + 0.5) as u32;
    log!(
        Level::Info,
        "tick {} at {} ms, dt {} ms",
        n,
        abs_time_ms,
        dt_ms
    );
    if n < 3 { Tick::Continue } else { Tick::Quit }
}

#[unsafe(no_mangle)]
pub extern "C" fn app_shutdown() {
    log!(
        Level::Info,
        "bye after {} ticks",
        TICKS.load(Ordering::Relaxed)
    );
}
