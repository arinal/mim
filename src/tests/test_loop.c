// SPDX-License-Identifier: MIT OR GPL-2.0-only

#include "mim_test.h"
#include "runtime/loop/loop.h"

#include <math.h>

static const uint64_t ms = 1000000;

// an arbitrary zero point, as pl_time_now_ns has
static const uint64_t t0 = 123456789012345;

static void test_dt(void) {
    loop_t loop = {0};
    loop_frame_t f = loop_begin(&loop, t0);
    CHECK(f.abs_time_ms == 0 && f.dt_seconds == 0.001f);

    f = loop_begin(&loop, t0 + loop_frame_ns);
    CHECK(f.abs_time_ms == 16 && fabsf(f.dt_seconds - 0.0166667f) < 1e-6f);

    // a debugger pause: dt as if it didn't happen, real time keeps going
    f = loop_begin(&loop, t0 + loop_frame_ns + 5000 * ms);
    CHECK(f.abs_time_ms == 5016 && f.dt_seconds == 0.001f);

    // two frames at the same time: dt is never 0
    f = loop_begin(&loop, t0 + loop_frame_ns + 5000 * ms);
    CHECK(f.dt_seconds == 0.001f);

    // every gap gives 0 < dt <= 1 s; exactly 1 s still counts as real
    uint64_t now = t0 + loop_frame_ns + 5000 * ms;
    for (uint64_t gap = 1; gap <= 3000 * ms; gap = gap * 3 + 1) {
        now += gap;
        f = loop_begin(&loop, now);
        CHECK(f.dt_seconds > 0.0f && f.dt_seconds <= 1.0f);
    }
    now += 1000 * ms;
    CHECK(loop_begin(&loop, now).dt_seconds == 1.0f);
}

static void test_deadline(void) {
    loop_t loop = {0};
    loop_begin(&loop, t0);

    // each frame works 5 ms and wakes 0.1 ms late; the grid doesn't drift
    uint64_t wake = t0;
    uint64_t deadline = 0;
    for (uint64_t i = 1; i <= 100; i++) {
        deadline = loop_deadline(&loop, wake + 5 * ms);
        CHECK(deadline == t0 + i * loop_frame_ns);
        wake = deadline + ms / 10;
        loop_begin(&loop, wake);
    }

    // a 200 ms frame: no wait, and the grid restarts at its end
    const uint64_t slow_end = wake + 200 * ms;
    CHECK(loop_deadline(&loop, slow_end) == slow_end);
    loop_begin(&loop, slow_end);
    CHECK(loop_deadline(&loop, slow_end + ms) == slow_end + loop_frame_ns);
}

int main(void) {
    test_dt();
    test_deadline();
    return 0;
}
