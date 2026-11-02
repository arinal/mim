// SPDX-License-Identifier: MIT OR GPL-2.0-only

//! Rows of tiles under the header. Left and right move along a row, which scrolls only when the
//! focus would come too close to its edge; up and down go to the tile under the same spot on
//! screen in the next row. The focused tile grows on a spring.

use crate::look::{self, GREY, HEADER_H, LEFT, W, WHITE};
use crate::{Action, Dir, Go};
use mim::{Level, cv, log};

pub const ROWS: usize = 4;
pub const TILES: usize = 10;

const TILE_W: f32 = 320.0;
const TILE_H: f32 = 180.0;
// from one tile's left edge to the next one's
const PITCH_X: f32 = TILE_W + 32.0;
// a row: its title, then its tiles
const ROWS_TOP: f32 = HEADER_H + 40.0;
const ROW_PITCH: f32 = 300.0;
const ROW_H: f32 = 260.0;
// a row scrolls to keep this much room on either side of the focus
const MARGIN_X: f32 = 120.0;
const MARGIN_Y: f32 = 120.0;
// how fast the scroll catches up: the share of the way it closes per second, roughly
const SCROLL_SPEED: f32 = 12.0;
// a spring of 0.3 s with a little bounce (SwiftUI's duration 0.3, bounce 0.15)
const STIFFNESS: f32 = 438.65;
const DAMPING: f32 = 35.6;
const FOCUS_ZOOM: f32 = 1.05;

/// A value that eases toward its target
#[derive(Clone, Copy)]
struct Smooth {
    now: f32,
    target: f32,
}

/// A spring's position and velocity; its target is wherever the focus says
#[derive(Clone, Copy)]
struct Spring {
    pos: f32,
    vel: f32,
}

pub struct Home {
    row: usize,
    // each row's focused tile, kept while the focus is in another row
    col: [usize; ROWS],
    scroll_x: [Smooth; ROWS],
    scroll_y: Smooth,
    zoom: [[Spring; TILES]; ROWS],
}

fn abs(x: f32) -> f32 {
    if x < 0.0 { -x } else { x }
}

/// The scroll that keeps [pos, pos + size) inside a window of `outer` over `content`, `margin`
/// from either edge, moving as little as possible from `prev`
fn classic(prev: f32, pos: f32, size: f32, outer: f32, content: f32, margin: f32) -> f32 {
    let mut s = prev;
    if pos - margin < s {
        s = pos - margin;
    } else if pos + size - outer + margin > s {
        s = pos + size - outer + margin;
    }
    let max = if content > outer {
        content - outer
    } else {
        0.0
    };
    if s < 0.0 {
        0.0
    } else if s > max {
        max
    } else {
        s
    }
}

pub fn load() -> Home {
    Home {
        row: 0,
        col: [0; ROWS],
        scroll_x: [Smooth {
            now: 0.0,
            target: 0.0,
        }; ROWS],
        scroll_y: Smooth {
            now: 0.0,
            target: 0.0,
        },
        zoom: [[Spring { pos: 1.0, vel: 0.0 }; TILES]; ROWS],
    }
}

// The centre of a tile on screen, once its row has scrolled where it's going
fn center_x(h: &Home, row: usize, col: usize) -> f32 {
    LEFT + col as f32 * PITCH_X + TILE_W / 2.0 - h.scroll_x[row].target
}

// Focuses `col` in the focused row, scrolling the rows to show it
fn focus(h: &mut Home, col: usize) {
    let row = h.row;
    h.col[row] = col;
    let content_x = TILES as f32 * PITCH_X - 32.0;
    let s = &mut h.scroll_x[row];
    s.target = classic(
        s.target,
        col as f32 * PITCH_X,
        TILE_W,
        W - 2.0 * LEFT,
        content_x,
        MARGIN_X,
    );
    let content_y = ROWS as f32 * ROW_PITCH - (ROW_PITCH - ROW_H);
    let s = &mut h.scroll_y;
    s.target = classic(
        s.target,
        row as f32 * ROW_PITCH,
        ROW_H,
        look::H - ROWS_TOP,
        content_y,
        MARGIN_Y,
    );
    log!(Level::Debug, "ui: focus {},{}", row, col);
}

// Moves the focus to `row`, onto the tile nearest the focused one's centre
fn go_to_row(h: &mut Home, row: usize) {
    let x = center_x(h, h.row, h.col[h.row]);
    let mut best = 0;
    for col in 1..TILES {
        if abs(center_x(h, row, col) - x) < abs(center_x(h, row, best) - x) {
            best = col;
        }
    }
    h.row = row;
    focus(h, best);
}

pub fn process_action(h: &mut Home, action: Action) -> Go {
    let col = h.col[h.row];
    match action {
        Action::Move(Dir::Left) if col > 0 => focus(h, col - 1),
        Action::Move(Dir::Right) if col + 1 < TILES => focus(h, col + 1),
        Action::Move(Dir::Up) if h.row > 0 => go_to_row(h, h.row - 1),
        Action::Move(Dir::Down) if h.row + 1 < ROWS => go_to_row(h, h.row + 1),
        Action::Move(_) => {}
        Action::Select => {
            log!(Level::Debug, "ui: details {},{}", h.row, col);
            return Go::Details(h.row, col);
        }
        // back goes to the top row first, and from there out of the app
        Action::Back if h.row > 0 => go_to_row(h, 0),
        Action::Back => return Go::Quit,
    }
    Go::Stay
}

fn ease(s: &mut Smooth, dt: f32) {
    let k = dt * SCROLL_SPEED;
    let k = if k > 1.0 { 1.0 } else { k };
    s.now += (s.target - s.now) * k;
    // closer than half a pixel is there
    if abs(s.target - s.now) < 0.5 {
        s.now = s.target;
    }
}

// Semi-implicit Euler in steps of at most 1/240 s, so a long frame can't make it blow up
fn spring(s: &mut Spring, target: f32, dt: f32) {
    if s.pos == target && s.vel == 0.0 {
        return;
    }
    let steps = (dt * 240.0) as u32 + 1;
    let step = dt / steps as f32;
    for _ in 0..steps {
        let accel = -STIFFNESS * (s.pos - target) - DAMPING * s.vel;
        s.vel += accel * step;
        s.pos += s.vel * step;
    }
    if abs(s.pos - target) <= 0.0005 && abs(s.vel) <= 0.0005 {
        s.pos = target;
        s.vel = 0.0;
    }
}

/// Moves the scrolls and springs on by `dt` seconds
pub fn animate(h: &mut Home, dt: f32) {
    for row in 0..ROWS {
        ease(&mut h.scroll_x[row], dt);
        for col in 0..TILES {
            let focused = row == h.row && col == h.col[row];
            spring(
                &mut h.zoom[row][col],
                if focused { FOCUS_ZOOM } else { 1.0 },
                dt,
            );
        }
    }
    ease(&mut h.scroll_y, dt);
}

fn tile(h: &Home, row: usize, col: usize, y: f32) {
    let x = LEFT + col as f32 * PITCH_X - h.scroll_x[row].now;
    // off screen, even grown
    if x + TILE_W + 16.0 < 0.0 || x - 16.0 > W {
        return;
    }
    let focused = row == h.row && col == h.col[row];
    look::tile(
        x,
        y,
        TILE_W,
        TILE_H,
        look::color(row, col),
        look::title(row, col),
        h.zoom[row][col].pos,
        focused,
    );
}

pub fn render(h: &Home) {
    look::background();
    for row in 0..ROWS {
        let y = ROWS_TOP + row as f32 * ROW_PITCH - h.scroll_y.now;
        if y + ROW_PITCH < HEADER_H || y > look::H {
            continue;
        }
        cv::font_size(30.0);
        cv::fill_style(if row == h.row { WHITE } else { GREY });
        cv::fill_text(look::ROW_TITLES[row], LEFT, y + 36.0);
        // the focused tile last, so it grows over its neighbours
        for col in 0..TILES {
            if row != h.row || col != h.col[row] {
                tile(h, row, col, y + 56.0);
            }
        }
        if row == h.row {
            tile(h, row, h.col[row], y + 56.0);
        }
    }
    look::header("4c \u{00B7} app UI");
    look::hint("Arrows move \u{00B7} OK opens \u{00B7} Back goes back");
}
