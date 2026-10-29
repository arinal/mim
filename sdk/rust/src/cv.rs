// SPDX-License-Identifier: MIT OR GPL-2.0-only

//! The canvas: 2D drawing shaped like HTML5's
//!
//! What's drawn shows when app_tick returns, and stays until it's drawn over. Coordinates are
//! pixels from the top left of the screen, before the transform. Colours are 0xAARRGGBB: alpha
//! 0xFF covers, less blends.
//!
//! Every function here is safe: the host checks what it's given. The ones that can hit a limit
//! (more than 32 saves, a path over 16384 points, a negative arc radius) make the host stop the
//! app, as a panic would.

use crate::ffi;

/// Pushes the state: the transform, the styles, the line width, the font size and the clip
pub fn save() {
    unsafe { ffi::cv_save() }
}

/// Pops the state; does nothing if nothing was saved
pub fn restore() {
    unsafe { ffi::cv_restore() }
}

pub fn translate(x: f32, y: f32) {
    unsafe { ffi::cv_translate(x, y) }
}

/// Text and line widths scale by sqrt(|x × y|)
pub fn scale(x: f32, y: f32) {
    unsafe { ffi::cv_scale(x, y) }
}

/// The colour of the fills and text that follow; it starts opaque black
pub fn fill_style(color: u32) {
    unsafe { ffi::cv_fill_style(color) }
}

/// The colour of the strokes that follow; it starts opaque black
pub fn stroke_style(color: u32) {
    unsafe { ffi::cv_stroke_style(color) }
}

/// It starts at 1; 0 or less is ignored
pub fn line_width(width: f32) {
    unsafe { ffi::cv_line_width(width) }
}

/// The font's em square; it starts at 10, and 0 or less is ignored
pub fn font_size(size: f32) {
    unsafe { ffi::cv_font_size(size) }
}

/// Narrows the clip to the rectangle, in whole pixels; only `restore` widens it again
pub fn clip_rect(x: f32, y: f32, width: f32, height: f32) {
    unsafe { ffi::cv_clip_rect(x, y, width, height) }
}

pub fn begin_path() {
    unsafe { ffi::cv_begin_path() }
}

pub fn move_to(x: f32, y: f32) {
    unsafe { ffi::cv_move_to(x, y) }
}

pub fn line_to(x: f32, y: f32) {
    unsafe { ffi::cv_line_to(x, y) }
}

/// An arc of the circle at (x, y), angles in radians, clockwise on screen unless
/// `counterclockwise`; a line joins it to the current point
pub fn arc(x: f32, y: f32, radius: f32, start: f32, end: f32, counterclockwise: bool) {
    unsafe { ffi::cv_arc(x, y, radius, start, end, counterclockwise as u32) }
}

/// A closed subpath; the radius is at most half the shorter side
pub fn rounded_rect(x: f32, y: f32, width: f32, height: f32, radius: f32) {
    unsafe { ffi::cv_rounded_rect(x, y, width, height, radius) }
}

pub fn close_path() {
    unsafe { ffi::cv_close_path() }
}

/// Fills the path by the non-zero winding rule
pub fn fill() {
    unsafe { ffi::cv_fill() }
}

/// Strokes the path: butt ends, mitred corners
pub fn stroke() {
    unsafe { ffi::cv_stroke() }
}

/// Fills a rectangle, leaving the path as it is
pub fn fill_rect(x: f32, y: f32, width: f32, height: f32) {
    unsafe { ffi::cv_fill_rect(x, y, width, height) }
}

/// An image the host decoded and keeps
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub struct Image(u32);

impl Image {
    /// Decodes a PNG, up to 4096 × 4096; None if it can't, or the host has 64 already
    pub fn load(png: &[u8]) -> Option<Image> {
        let handle = unsafe { ffi::cv_image_load(png.as_ptr(), png.len() as u32) };
        (handle != 0).then_some(Image(handle))
    }

    /// Draws it scaled into the rectangle, smoothed
    pub fn draw(self, x: f32, y: f32, width: f32, height: f32) {
        unsafe { ffi::cv_draw_image(self.0, x, y, width, height) }
    }
}

/// Draws text in the built-in font (Liberation Sans, Latin) on its baseline at (x, y)
pub fn fill_text(text: &str, x: f32, y: f32) {
    unsafe { ffi::cv_fill_text(text.as_ptr(), text.len() as u32, x, y) }
}

/// How wide `fill_text` would draw it, before the transform
pub fn measure_text(text: &str) -> f32 {
    unsafe { ffi::cv_measure_text(text.as_ptr(), text.len() as u32) }
}
