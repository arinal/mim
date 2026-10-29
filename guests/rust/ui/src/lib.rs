// SPDX-License-Identifier: MIT OR GPL-2.0-only

//! A TV-style app: rows of tiles to move through with the remote, and a page for the one picked.
//! Immediate mode: one `State`, changed only by `Action`s, and every frame drawn again from it.
//! guests/cpp/ui.cpp is the same app, pixel for pixel.

#![no_std]

mod details;
mod home;
mod look;

use mim::input::{self, Key};
use mim::{App, Level, Tick, cv, log};

/// What the user asked for; a held direction repeats
#[derive(Clone, Copy, PartialEq)]
pub enum Action {
    Move(Dir),
    Select,
    Back,
}

#[derive(Clone, Copy, PartialEq)]
pub enum Dir {
    Left,
    Right,
    Up,
    Down,
}

/// Where a screen's process_action says to go
pub enum Go {
    Stay,
    Details(usize, usize),
    Home,
    Quit,
}

#[derive(Clone, Copy, PartialEq)]
enum Screen {
    Home,
    Details,
}

// a held direction moves again after this many seconds, then every interval
const REPEAT_DELAY: f32 = 0.30;
const REPEAT_INTERVAL: f32 = 0.15;
// a longer frame (a pause in a debugger) counts as this long
const MAX_DT: f32 = 0.1;

/// Everything the app knows
struct State {
    screen: Screen,
    home: home::Home,
    details: details::Details,
    // the direction held down, for how long, and when it next repeats
    held: Option<Dir>,
    held_for: f32,
    next_repeat: f32,
    quit: bool,
}

fn dir(key: Key) -> Option<Dir> {
    match key {
        Key::Left => Some(Dir::Left),
        Key::Right => Some(Dir::Right),
        Key::Up => Some(Dir::Up),
        Key::Down => Some(Dir::Down),
        _ => None,
    }
}

impl State {
    fn process_action(&mut self, action: Action) {
        let go = match self.screen {
            Screen::Home => home::process_action(&mut self.home, action),
            Screen::Details => details::process_action(&self.details, action),
        };
        match go {
            Go::Stay => {}
            Go::Details(row, col) => {
                self.details = details::load(row, col);
                self.screen = Screen::Details;
            }
            Go::Home => {
                log(Level::Debug, "ui: home");
                self.screen = Screen::Home;
            }
            Go::Quit => self.quit = true,
        }
    }

    // Turns this frame's key events, and a key still held, into actions
    fn read_input(&mut self, dt: f32) {
        if let Some(d) = self.held {
            self.held_for += dt;
            while self.held_for >= self.next_repeat {
                self.next_repeat += REPEAT_INTERVAL;
                self.process_action(Action::Move(d));
            }
        }
        while let Some(event) = input::next_event() {
            match (event.key, event.pressed) {
                (Key::Ok, true) => self.process_action(Action::Select),
                (Key::Back, true) => self.process_action(Action::Back),
                (key, true) => {
                    if let Some(d) = dir(key) {
                        self.held = Some(d);
                        self.held_for = 0.0;
                        self.next_repeat = REPEAT_DELAY;
                        self.process_action(Action::Move(d));
                    }
                }
                (key, false) => {
                    if dir(key).is_some() && dir(key) == self.held {
                        self.held = None;
                    }
                }
            }
        }
    }
}

impl App for State {
    fn init() -> Self {
        State {
            screen: Screen::Home,
            home: home::load(),
            details: details::load(0, 0),
            held: None,
            held_for: 0.0,
            next_repeat: 0.0,
            quit: false,
        }
    }

    fn tick(&mut self, _abs_time_ms: u64, dt_seconds: f32) -> Tick {
        let dt = if dt_seconds > MAX_DT {
            MAX_DT
        } else {
            dt_seconds
        };
        self.read_input(dt);
        if self.quit {
            log(Level::Debug, "ui: bye");
            return Tick::Quit;
        }
        // the rows keep animating behind the details page, so they're settled on the way back
        home::animate(&mut self.home, dt);

        // laid out on a 1920 x 1080 screen, scaled to the real one here, once
        cv::save();
        cv::scale(look::SCALE, look::SCALE);
        match self.screen {
            Screen::Home => home::render(&self.home),
            Screen::Details => details::render(&self.details),
        }
        cv::restore();
        Tick::Continue
    }
}

mim::app!(State);
