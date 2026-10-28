//===-- loop.h - Frame timing: app_tick's arguments, the schedule ---------===//
//
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// Pure arithmetic on the timestamps it's given, with no clock inside, so a
// test can feed it any times it likes.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    /// 60 Hz; the window step replaces this with vsync
    loop_frame_ns = 16666667,
};

/// The frame timing state; starts zeroed, the fields are private to loop.c
typedef struct loop_t {
    struct {
        uint64_t start_ns;
        uint64_t last_ns;
        uint64_t deadline_ns;
        bool started;
    } internal;
} loop_t;

/// What one app_tick gets
typedef struct loop_frame_t {
    /// Milliseconds since the first frame
    uint64_t abs_time_ms;
    /// Seconds since the last frame; 0.001 for the first frame and after a gap over 1 s
    float dt_seconds;
} loop_frame_t;

/// Starts a frame at `now_ns`, the frame's time event
loop_frame_t loop_begin(loop_t *const loop, const uint64_t now_ns);

/// Returns when the next frame is due, given that this one's work ended at `now_ns`
///
/// Frames stay on a fixed grid. A frame that overran gets `now_ns` (no wait), and the grid
/// restarts there instead of running frames back to back to catch up.
uint64_t loop_deadline(loop_t *const loop, const uint64_t now_ns);

#ifdef __cplusplus
}
#endif
