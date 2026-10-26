// SPDX-License-Identifier: MIT OR GPL-2.0-only

#include "mim_test.h"
#include "platform/pl.h"

#include <signal.h>

int main(void) {
    pl_event_t events[4];

    // nothing happened: only the time, which never goes backwards
    uint64_t prev = 0;
    for (int i = 0; i < 100; i++) {
        CHECK(pl_tick(events, 4) == 1);
        CHECK(events[0].type == pl_event_time);
        CHECK(events[0].time_ns >= prev);
        prev = events[0].time_ns;
    }

    // Ctrl-C before the next tick: a quit, then the time; then back to normal
    CHECK(raise(SIGINT) == 0);
    CHECK(pl_tick(events, 4) == 2);
    CHECK(events[0].type == pl_event_quit && events[1].type == pl_event_time);
    CHECK(events[0].time_ns <= events[1].time_ns);
    CHECK(pl_tick(events, 4) == 1 && events[0].type == pl_event_time);

    CHECK(raise(SIGTERM) == 0);
    CHECK(pl_tick(events, 2) == 2 && events[0].type == pl_event_quit);

    // an absolute deadline: never early, and a past one doesn't sleep
    const uint64_t start = pl_time_now_ns();
    pl_sleep_until_ns(start + 50 * 1000000u);
    const uint64_t woke = pl_time_now_ns();
    CHECK(woke >= start + 50 * 1000000u);
    // only a loose upper bound: a busy CI machine can pause us for a long time
    CHECK(woke - start < 1000 * 1000000u);
    pl_sleep_until_ns(start);
    CHECK(pl_time_now_ns() - woke < 1000 * 1000000u);
    return 0;
}
