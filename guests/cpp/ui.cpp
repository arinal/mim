//===-- ui.cpp - The app UI, frame for frame as guests/rust/ui ------------===//
//
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// A TV-style app: rows of tiles to move through with the remote, and a page
// for the one picked. Immediate mode: one State, changed only by Actions, and
// every frame drawn again from it. The Rust guest has the same parts, in the
// same order, with the same float operations; every literal is a float.
//
//===----------------------------------------------------------------------===//

#include "mim.h"

namespace {

namespace cv = mim::cv;

enum class Dir { left,
                 right,
                 up,
                 down };

/// What the user asked for; a held direction repeats
struct Action {
    enum Kind { move,
                select,
                back } kind;
    Dir dir;
};

/// Where a screen's process_action says to go
struct Go {
    enum Kind { stay,
                details,
                home,
                quit } kind;
    uint32_t row;
    uint32_t col;
};

float abs(float x) {
    return x < 0.0f ? -x : x;
}

//===----------------------------------------------------------------------===//
// What every screen shares: the logical screen, the colours, the names, and
// the pieces drawn on more than one screen
//===----------------------------------------------------------------------===//

namespace look {

// Everything is laid out on this screen; one scale fits it to the real one
constexpr float W = 1920.0f;
constexpr float H = 1080.0f;
constexpr float SCALE = float(mim::screen_width) / W;

constexpr float LEFT = 96.0f;
constexpr float HEADER_H = 160.0f;

constexpr uint32_t BACKGROUND = 0xFF0E1117;
constexpr uint32_t WHITE = 0xFFFFFFFF;
constexpr uint32_t GREY = 0xFF8B949E;
constexpr uint32_t LINE = 0xFF30363D;
constexpr uint32_t PALETTE[8] = {0xFF1F6FEB, 0xFF8957E5, 0xFFBF4B8A, 0xFFD1242F, 0xFFBC4C00, 0xFF9A6700, 0xFF1A7F37, 0xFF0E7490};
const char *const NAMES[16] = {"Mountains", "Sunset", "Ridge", "Dusk", "Peaks", "Harbour", "Glacier", "Canyon", "Lagoon", "Tundra", "Meadow", "Delta", "Summit", "Reef", "Dune", "Fjord"};
const char *const ROW_TITLES[4] = {"Continue watching", "Trending now", "Movies", "Series"};

uint32_t color(uint32_t row, uint32_t col) {
    return PALETTE[(row * 3 + col) % 8];
}

const char *title(uint32_t row, uint32_t col) {
    return NAMES[(row * 7 + col * 3) % 16];
}

void background() {
    cv::fill_style(BACKGROUND);
    cv::fill_rect(0.0f, 0.0f, W, H);
}

// The bar along the top, over anything scrolled under it, with `label` on the right
void header(const char *label) {
    cv::fill_style(BACKGROUND);
    cv::fill_rect(0.0f, 0.0f, W, HEADER_H);
    cv::font_size(56.0f);
    cv::fill_style(WHITE);
    cv::fill_text("MIM", LEFT, 104.0f);
    const float mim = cv::measure_text("MIM");
    cv::font_size(28.0f);
    cv::fill_style(GREY);
    cv::fill_text("isn't M5", LEFT + mim + 24.0f, 104.0f);
    cv::fill_text(label, W - LEFT - cv::measure_text(label), 104.0f);
    cv::fill_style(LINE);
    cv::fill_rect(LEFT, 138.0f, W - 2.0f * LEFT, 2.0f);
}

// A translucent bar along the bottom, `text` centred in it
void hint(const char *text) {
    cv::fill_style(0xC0000000);
    cv::fill_rect(0.0f, H - 96.0f, W, 96.0f);
    cv::font_size(28.0f);
    cv::fill_style(WHITE);
    cv::fill_text(text, (W - cv::measure_text(text)) / 2.0f, H - 38.0f);
}

// A rounded tile with its title, grown by `zoom` around its centre, ringed if focused
void tile(float x, float y, float w, float h, uint32_t color, const char *title, float zoom, bool focused) {
    cv::save();
    const float cx = x + w / 2.0f;
    const float cy = y + h / 2.0f;
    cv::translate(cx, cy);
    cv::scale(zoom, zoom);
    cv::translate(-cx, -cy);
    cv::begin_path();
    cv::rounded_rect(x, y, w, h, 16.0f);
    cv::fill_style(color);
    cv::fill();
    cv::font_size(26.0f);
    cv::fill_style(WHITE);
    cv::fill_text(title, x + 20.0f, y + h - 22.0f);
    if (focused) {
        cv::stroke_style(WHITE);
        cv::line_width(6.0f);
        cv::begin_path();
        cv::rounded_rect(x - 9.0f, y - 9.0f, w + 18.0f, h + 18.0f, 24.0f);
        cv::stroke();
    }
    cv::restore();
}

} // namespace look

//===----------------------------------------------------------------------===//
// Home: rows of tiles under the header. Left and right move along a row,
// which scrolls only when the focus would come too close to its edge; up and
// down go to the tile under the same spot on screen in the next row. The
// focused tile grows on a spring.
//===----------------------------------------------------------------------===//

namespace home {

using look::HEADER_H;
using look::LEFT;
using look::W;

constexpr uint32_t ROWS = 4;
constexpr uint32_t TILES = 10;

constexpr float TILE_W = 320.0f;
constexpr float TILE_H = 180.0f;
// from one tile's left edge to the next one's
constexpr float PITCH_X = TILE_W + 32.0f;
// a row: its title, then its tiles
constexpr float ROWS_TOP = HEADER_H + 40.0f;
constexpr float ROW_PITCH = 300.0f;
constexpr float ROW_H = 260.0f;
// a row scrolls to keep this much room on either side of the focus
constexpr float MARGIN_X = 120.0f;
constexpr float MARGIN_Y = 120.0f;
// how fast the scroll catches up: the share of the way it closes per second, roughly
constexpr float SCROLL_SPEED = 12.0f;
// a spring of 0.3 s with a little bounce (SwiftUI's duration 0.3, bounce 0.15)
constexpr float STIFFNESS = 438.65f;
constexpr float DAMPING = 35.6f;
constexpr float FOCUS_ZOOM = 1.05f;

// A value that eases toward its target
struct Smooth {
    float now;
    float target;
};

// A spring's position and velocity; its target is wherever the focus says
struct Spring {
    float pos;
    float vel;
};

struct Home {
    uint32_t row;
    // each row's focused tile, kept while the focus is in another row
    uint32_t col[ROWS];
    Smooth scroll_x[ROWS];
    Smooth scroll_y;
    Spring zoom[ROWS][TILES];
};

// The scroll that keeps [pos, pos + size) inside a window of `outer` over `content`, `margin`
// from either edge, moving as little as possible from `prev`
float classic(float prev, float pos, float size, float outer, float content, float margin) {
    float s = prev;
    if (pos - margin < s) {
        s = pos - margin;
    } else if (pos + size - outer + margin > s) {
        s = pos + size - outer + margin;
    }
    const float max = content > outer ? content - outer : 0.0f;
    if (s < 0.0f) {
        return 0.0f;
    } else if (s > max) {
        return max;
    }
    return s;
}

Home load() {
    Home h = {};
    for (uint32_t row = 0; row < ROWS; row++) {
        for (uint32_t col = 0; col < TILES; col++) {
            h.zoom[row][col] = {1.0f, 0.0f};
        }
    }
    return h;
}

// The centre of a tile on screen, once its row has scrolled where it's going
float center_x(const Home &h, uint32_t row, uint32_t col) {
    return LEFT + float(col) * PITCH_X + TILE_W / 2.0f - h.scroll_x[row].target;
}

// Focuses `col` in the focused row, scrolling the rows to show it
void focus(Home &h, uint32_t col) {
    const uint32_t row = h.row;
    h.col[row] = col;
    const float content_x = float(TILES) * PITCH_X - 32.0f;
    Smooth &sx = h.scroll_x[row];
    sx.target = classic(sx.target, float(col) * PITCH_X, TILE_W, W - 2.0f * LEFT, content_x, MARGIN_X);
    const float content_y = float(ROWS) * ROW_PITCH - (ROW_PITCH - ROW_H);
    Smooth &sy = h.scroll_y;
    sy.target = classic(sy.target, float(row) * ROW_PITCH, ROW_H, look::H - ROWS_TOP, content_y, MARGIN_Y);
    (mim::Line() << "ui: focus " << row << "," << col).send(mim::Level::debug);
}

// Moves the focus to `row`, onto the tile nearest the focused one's centre
void go_to_row(Home &h, uint32_t row) {
    const float x = center_x(h, h.row, h.col[h.row]);
    uint32_t best = 0;
    for (uint32_t col = 1; col < TILES; col++) {
        if (abs(center_x(h, row, col) - x) < abs(center_x(h, row, best) - x)) {
            best = col;
        }
    }
    h.row = row;
    focus(h, best);
}

Go process_action(Home &h, Action action) {
    const uint32_t col = h.col[h.row];
    switch (action.kind) {
        case Action::move:
            if (action.dir == Dir::left && col > 0) {
                focus(h, col - 1);
            } else if (action.dir == Dir::right && col + 1 < TILES) {
                focus(h, col + 1);
            } else if (action.dir == Dir::up && h.row > 0) {
                go_to_row(h, h.row - 1);
            } else if (action.dir == Dir::down && h.row + 1 < ROWS) {
                go_to_row(h, h.row + 1);
            }
            break;
        case Action::select:
            (mim::Line() << "ui: details " << h.row << "," << col).send(mim::Level::debug);
            return {Go::details, h.row, col};
        case Action::back:
            // back goes to the top row first, and from there out of the app
            if (h.row == 0) {
                return {Go::quit, 0, 0};
            }
            go_to_row(h, 0);
            break;
    }
    return {Go::stay, 0, 0};
}

void ease(Smooth &s, float dt) {
    float k = dt * SCROLL_SPEED;
    k = k > 1.0f ? 1.0f : k;
    s.now += (s.target - s.now) * k;
    // closer than half a pixel is there
    if (abs(s.target - s.now) < 0.5f) {
        s.now = s.target;
    }
}

// Semi-implicit Euler in steps of at most 1/240 s, so a long frame can't make it blow up
void spring(Spring &s, float target, float dt) {
    if (s.pos == target && s.vel == 0.0f) {
        return;
    }
    const uint32_t steps = uint32_t(dt * 240.0f) + 1;
    const float step = dt / float(steps);
    for (uint32_t i = 0; i < steps; i++) {
        const float accel = -STIFFNESS * (s.pos - target) - DAMPING * s.vel;
        s.vel += accel * step;
        s.pos += s.vel * step;
    }
    if (abs(s.pos - target) <= 0.0005f && abs(s.vel) <= 0.0005f) {
        s.pos = target;
        s.vel = 0.0f;
    }
}

// Moves the scrolls and springs on by `dt` seconds
void animate(Home &h, float dt) {
    for (uint32_t row = 0; row < ROWS; row++) {
        ease(h.scroll_x[row], dt);
        for (uint32_t col = 0; col < TILES; col++) {
            const bool focused = row == h.row && col == h.col[row];
            spring(h.zoom[row][col], focused ? FOCUS_ZOOM : 1.0f, dt);
        }
    }
    ease(h.scroll_y, dt);
}

void tile(const Home &h, uint32_t row, uint32_t col, float y) {
    const float x = LEFT + float(col) * PITCH_X - h.scroll_x[row].now;
    // off screen, even grown
    if (x + TILE_W + 16.0f < 0.0f || x - 16.0f > W) {
        return;
    }
    const bool focused = row == h.row && col == h.col[row];
    look::tile(x, y, TILE_W, TILE_H, look::color(row, col), look::title(row, col), h.zoom[row][col].pos, focused);
}

void render(const Home &h) {
    look::background();
    for (uint32_t row = 0; row < ROWS; row++) {
        const float y = ROWS_TOP + float(row) * ROW_PITCH - h.scroll_y.now;
        if (y + ROW_PITCH < HEADER_H || y > look::H) {
            continue;
        }
        cv::font_size(30.0f);
        cv::fill_style(row == h.row ? look::WHITE : look::GREY);
        cv::fill_text(look::ROW_TITLES[row], LEFT, y + 36.0f);
        // the focused tile last, so it grows over its neighbours
        for (uint32_t col = 0; col < TILES; col++) {
            if (row != h.row || col != h.col[row]) {
                tile(h, row, col, y + 56.0f);
            }
        }
        if (row == h.row) {
            tile(h, row, h.col[row], y + 56.0f);
        }
    }
    look::header("4c · app UI");
    look::hint("Arrows move · OK opens · Back goes back");
}

} // namespace home

//===----------------------------------------------------------------------===//
// Details: the page for one tile, with its artwork, its name, and a Play button
//===----------------------------------------------------------------------===//

namespace details {

using look::LEFT;

struct Details {
    uint32_t row;
    uint32_t col;
};

Details load(uint32_t row, uint32_t col) {
    return {row, col};
}

Go process_action(const Details &d, Action action) {
    switch (action.kind) {
        case Action::back:
            return {Go::home, 0, 0};
        case Action::select:
            (mim::Line() << "ui: play " << look::title(d.row, d.col)).send(mim::Level::debug);
            break;
        case Action::move:
            break;
    }
    return {Go::stay, 0, 0};
}

void render(const Details &d) {
    look::background();
    cv::begin_path();
    cv::rounded_rect(LEFT, 200.0f, 1024.0f, 576.0f, 24.0f);
    cv::fill_style(look::color(d.row, d.col));
    cv::fill();

    const float x = LEFT + 1024.0f + 64.0f;
    cv::font_size(64.0f);
    cv::fill_style(look::WHITE);
    cv::fill_text(look::title(d.row, d.col), x, 290.0f);
    cv::font_size(30.0f);
    cv::fill_style(look::GREY);
    cv::fill_text(look::ROW_TITLES[d.row], x, 350.0f);

    // the button, focused: white with dark text
    cv::begin_path();
    cv::rounded_rect(x, 420.0f, 240.0f, 72.0f, 36.0f);
    cv::fill_style(look::WHITE);
    cv::fill();
    cv::font_size(30.0f);
    cv::fill_style(look::BACKGROUND);
    cv::fill_text("Play", x + (240.0f - cv::measure_text("Play")) / 2.0f, 466.0f);

    look::header("4c · details");
    look::hint("OK plays · Back goes back");
}

} // namespace details

//===----------------------------------------------------------------------===//
// The app: its state, and turning keys into actions
//===----------------------------------------------------------------------===//

// a held direction moves again after this many seconds, then every interval
constexpr float REPEAT_DELAY = 0.30f;
constexpr float REPEAT_INTERVAL = 0.15f;
// a longer frame (a pause in a debugger) counts as this long
constexpr float MAX_DT = 0.1f;

enum class Screen { home,
                    details };

// Everything the app knows
struct State {
    Screen screen;
    home::Home home;
    details::Details details;
    // the direction held down (if `holding`), for how long, and when it next repeats
    bool holding;
    Dir held;
    float held_for;
    float next_repeat;
    bool quit;
};

State state;

bool dir(mim::Key key, Dir &out) {
    switch (key) {
        case mim::Key::left:
            out = Dir::left;
            return true;
        case mim::Key::right:
            out = Dir::right;
            return true;
        case mim::Key::up:
            out = Dir::up;
            return true;
        case mim::Key::down:
            out = Dir::down;
            return true;
        default:
            return false;
    }
}

void process_action(Action action) {
    const Go go = state.screen == Screen::home ? home::process_action(state.home, action)
                                               : details::process_action(state.details, action);
    switch (go.kind) {
        case Go::stay:
            break;
        case Go::details:
            state.details = details::load(go.row, go.col);
            state.screen = Screen::details;
            break;
        case Go::home:
            mim::log(mim::Level::debug, "ui: home");
            state.screen = Screen::home;
            break;
        case Go::quit:
            state.quit = true;
            break;
    }
}

// Turns this frame's key events, and a key still held, into actions
void read_input(float dt) {
    if (state.holding) {
        state.held_for += dt;
        while (state.held_for >= state.next_repeat) {
            state.next_repeat += REPEAT_INTERVAL;
            process_action({Action::move, state.held});
        }
    }
    mim::Event event;
    while (mim::next_event(event)) {
        Dir d;
        if (event.pressed && event.key == mim::Key::ok) {
            process_action({Action::select, Dir::left});
        } else if (event.pressed && event.key == mim::Key::back) {
            process_action({Action::back, Dir::left});
        } else if (event.pressed && dir(event.key, d)) {
            state.holding = true;
            state.held = d;
            state.held_for = 0.0f;
            state.next_repeat = REPEAT_DELAY;
            process_action({Action::move, d});
        } else if (!event.pressed && dir(event.key, d) && state.holding && d == state.held) {
            state.holding = false;
        }
    }
}

} // namespace

extern "C" {

void app_init() {
    state.screen = Screen::home;
    state.home = home::load();
    state.details = details::load(0, 0);
    state.holding = false;
    state.held_for = 0.0f;
    state.next_repeat = 0.0f;
    state.quit = false;
}

int32_t app_tick(uint64_t abs_time_ms, float dt_seconds) {
    (void)abs_time_ms;
    const float dt = dt_seconds > MAX_DT ? MAX_DT : dt_seconds;
    read_input(dt);
    if (state.quit) {
        mim::log(mim::Level::debug, "ui: bye");
        return int32_t(mim::Tick::quit);
    }
    // the rows keep animating behind the details page, so they're settled on the way back
    home::animate(state.home, dt);

    // laid out on a 1920 x 1080 screen, scaled to the real one here, once
    cv::save();
    cv::scale(look::SCALE, look::SCALE);
    if (state.screen == Screen::home) {
        home::render(state.home);
    } else {
        details::render(state.details);
    }
    cv::restore();
    return int32_t(mim::Tick::keep_going);
}

void app_shutdown() {
}
}
