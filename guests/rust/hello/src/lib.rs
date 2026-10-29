// SPDX-License-Identifier: MIT OR GPL-2.0-only

//! Logs the same lines as the C++ hello guest

#![no_std]

extern crate alloc;

use alloc::vec::Vec;
use mim::{App, Level, Tick, log};

struct Hello {
    ticks: u32,
}

impl App for Hello {
    fn init() -> Self {
        log(Level::Info, "hello");
        // 400 KB: more than the guest starts with, so this grows linear memory
        let numbers: Vec<u32> = (0..100_000).collect();
        let sum: u64 = numbers.iter().map(|&n| u64::from(n)).sum();
        log!(Level::Info, "sum of 0..99999: {}", sum);
        Hello { ticks: 0 }
    }

    fn tick(&mut self, abs_time_ms: u64, dt_seconds: f32) -> Tick {
        self.ticks += 1;
        let dt_ms = (dt_seconds * 1000.0 + 0.5) as u32;
        log!(
            Level::Info,
            "tick {} at {} ms, dt {} ms",
            self.ticks,
            abs_time_ms,
            dt_ms
        );
        if self.ticks < 3 {
            Tick::Continue
        } else {
            Tick::Quit
        }
    }

    fn shutdown(&mut self) {
        log!(Level::Info, "bye after {} ticks", self.ticks);
    }
}

mim::app!(Hello);
