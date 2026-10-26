//===-- loop.c - Frame timing ---------------------------------------------===//
//
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// The dt rules are M5's (app_event_loop_pre_tick): no zero dt, and a pause
// over 1 s counts as 1 ms, so a debugger stop doesn't make animations jump.
//
//===----------------------------------------------------------------------===//

#include "runtime/loop/loop.h"

#include <assert.h>

enum {
    loop_max_gap_ns = 1000000000,
};

static const float loop_min_dt = 0.001f;

loop_frame_t loop_begin(loop_t * const loop, const uint64_t now_ns) {
    if (!loop->internal.started) {
        loop->internal.started = true;
        loop->internal.start_ns = now_ns;
        loop->internal.last_ns = now_ns;
        loop->internal.deadline_ns = now_ns;
        return (loop_frame_t){0, loop_min_dt};
    }
    // the clock is monotonic: anything else is a platform bug
    assert(now_ns >= loop->internal.last_ns);
    const uint64_t gap = now_ns - loop->internal.last_ns;
    loop->internal.last_ns = now_ns;
    const float dt = (gap == 0 || gap > loop_max_gap_ns) ? loop_min_dt : (float)gap / 1e9f;
    return (loop_frame_t){(now_ns - loop->internal.start_ns) / 1000000, dt};
}

uint64_t loop_deadline(loop_t * const loop, const uint64_t now_ns) {
    assert(loop->internal.started);
    loop->internal.deadline_ns += loop_frame_ns;
    if (loop->internal.deadline_ns <= now_ns) {
        loop->internal.deadline_ns = now_ns;
    }
    return loop->internal.deadline_ns;
}
