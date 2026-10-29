// SPDX-License-Identifier: MIT OR GPL-2.0-only

//! Writes the guest imports' glue, for the host and both SDKs, from sdk/mim.ffi
//!
//! `cargo run -p ffigen`, from anywhere in the repo. The outputs are checked in.

mod output;
mod parse;
mod table;

use std::fs;
use std::path::Path;
use std::process::ExitCode;

use output::{guest_c, host_c, host_h, rust};
use parse::parse;
use table::Table;

pub const TABLE: &str = "sdk/mim.ffi";

type Generate = fn(&Table) -> String;

// What gets written where
const OUTPUTS: [(&str, Generate); 4] = [
    ("runtime/guest/ffi.h", host_h),
    ("runtime/guest/ffi.c", host_c),
    ("sdk/rust/src/ffi.rs", rust),
    ("sdk/cpp/ffi.h", guest_c),
];

fn run() -> Result<(), Vec<String>> {
    let root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    let text = fs::read_to_string(root.join(TABLE)).map_err(|e| vec![format!("{TABLE}: {e}")])?;
    let table = parse(&text)?;
    OUTPUTS.iter().try_for_each(|(path, generate)| {
        fs::write(root.join(path), generate(&table)).map_err(|e| vec![format!("{path}: {e}")])
    })
}

fn main() -> ExitCode {
    match run() {
        Ok(()) => ExitCode::SUCCESS,
        Err(errors) => {
            for e in &errors {
                eprintln!("ffigen: {e}");
            }
            ExitCode::FAILURE
        }
    }
}
