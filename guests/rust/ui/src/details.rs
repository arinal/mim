// SPDX-License-Identifier: MIT OR GPL-2.0-only

//! The page for one tile: its artwork, its name, and a Play button

use crate::look::{self, GREY, LEFT, WHITE};
use crate::{Action, Go};
use mim::{Level, cv, log};

pub struct Details {
    row: usize,
    col: usize,
}

pub fn load(row: usize, col: usize) -> Details {
    Details { row, col }
}

pub fn process_action(d: &Details, action: Action) -> Go {
    match action {
        Action::Back => Go::Home,
        Action::Select => {
            log!(Level::Debug, "ui: play {}", look::title(d.row, d.col));
            Go::Stay
        }
        Action::Move(_) => Go::Stay,
    }
}

pub fn render(d: &Details) {
    look::background();
    cv::begin_path();
    cv::rounded_rect(LEFT, 200.0, 1024.0, 576.0, 24.0);
    cv::fill_style(look::color(d.row, d.col));
    cv::fill();

    let x = LEFT + 1024.0 + 64.0;
    cv::font_size(64.0);
    cv::fill_style(WHITE);
    cv::fill_text(look::title(d.row, d.col), x, 290.0);
    cv::font_size(30.0);
    cv::fill_style(GREY);
    cv::fill_text(look::ROW_TITLES[d.row], x, 350.0);

    // the button, focused: white with dark text
    cv::begin_path();
    cv::rounded_rect(x, 420.0, 240.0, 72.0, 36.0);
    cv::fill_style(WHITE);
    cv::fill();
    cv::font_size(30.0);
    cv::fill_style(look::BACKGROUND);
    cv::fill_text("Play", x + (240.0 - cv::measure_text("Play")) / 2.0, 466.0);

    look::header("4c \u{00B7} details");
    look::hint("OK plays \u{00B7} Back goes back");
}
