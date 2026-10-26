// SPDX-License-Identifier: MIT OR GPL-2.0-only

//! The thin layer between a MIM app and the host's raw imports

#![no_std]

use core::fmt::{self, Write};

#[global_allocator]
static ALLOC: dlmalloc::GlobalDlmalloc = dlmalloc::GlobalDlmalloc;

mod raw {
    // wasm32v1-none links without --allow-undefined: imports must name their module
    #[link(wasm_import_module = "mim")]
    unsafe extern "C" {
        pub fn log(level: i32, ptr: *const u8, len: usize);
    }
}

/// The severity of a log line, as the host numbers them
#[derive(Clone, Copy)]
pub enum Level {
    Debug = 0,
    Info = 1,
    Warn = 2,
    Error = 3,
}

/// What app_tick tells the host
#[repr(i32)]
pub enum Tick {
    Quit = 0,
    Continue = 1,
}

/// Writes one line to the host's log
pub fn log(level: Level, msg: &str) {
    // the host checks the pointer and length; a valid &str always passes
    unsafe { raw::log(level as i32, msg.as_ptr(), msg.len()) }
}

/// A line being formatted on the stack; text past its capacity is dropped
pub struct Line {
    buf: [u8; 200],
    len: usize,
}

impl Line {
    pub const fn new() -> Self {
        Line {
            buf: [0; 200],
            len: 0,
        }
    }

    pub fn as_str(&self) -> &str {
        // write_str only ever copies whole &strs, and stops before cutting one
        unsafe { core::str::from_utf8_unchecked(&self.buf[..self.len]) }
    }
}

impl Write for Line {
    fn write_str(&mut self, s: &str) -> fmt::Result {
        if s.len() > self.buf.len() - self.len {
            return Err(fmt::Error);
        }
        self.buf[self.len..self.len + s.len()].copy_from_slice(s.as_bytes());
        self.len += s.len();
        Ok(())
    }
}

/// `log!(Level::Info, "x = {}", x)`: formats on the stack, then logs
#[macro_export]
macro_rules! log {
    ($level:expr, $($arg:tt)*) => {{
        use core::fmt::Write as _;
        let mut line = $crate::Line::new();
        let _ = write!(line, $($arg)*);
        $crate::log($level, line.as_str());
    }};
}

#[panic_handler]
fn panic(info: &core::panic::PanicInfo) -> ! {
    log!(Level::Error, "panic: {}", info.message());
    // traps: the host logs it and drops the guest
    core::arch::wasm32::unreachable()
}
