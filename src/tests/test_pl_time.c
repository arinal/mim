// SPDX-License-Identifier: MIT OR GPL-2.0-only

#include "mim_test.h"
#include "platform/pl.h"

int main(void) {
    uint64_t prev = pl_time_now_ns();
    for (int i = 0; i < 1000; ++i) {
        const uint64_t now = pl_time_now_ns();
        CHECK(now >= prev);
        prev = now;
    }

    const uint64_t start = pl_time_now_ns();
    pl_sleep_ms(50);
    const uint64_t elapsed_ms = (pl_time_now_ns() - start) / 1000000;
    CHECK(elapsed_ms >= 50);
    // Only a loose upper bound: a busy CI machine can pause us for a long time.
    CHECK(elapsed_ms < 1000);
    return 0;
}
