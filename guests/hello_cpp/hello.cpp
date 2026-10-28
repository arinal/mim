//===-- hello.cpp - Logs the same lines as the Rust hello guest -----------===//
//
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
//===----------------------------------------------------------------------===//

#include "mim.h"

namespace {
uint32_t ticks;
}

extern "C" {

void app_init() {
    mim::log(mim::Level::info, "hello");
    // 400 KB: more than the guest starts with, so this grows linear memory
    const uint32_t count = 100000;
    uint32_t *const numbers = new uint32_t[count];
    for (uint32_t i = 0; i < count; i++) {
        numbers[i] = i;
    }
    uint64_t sum = 0;
    for (uint32_t i = 0; i < count; i++) {
        sum += numbers[i];
    }
    delete[] numbers;
    (mim::Line() << "sum of 0..99999: " << sum).send(mim::Level::info);
}

mim::Tick app_tick(uint64_t abs_time_ms, float dt_seconds) {
    ticks++;
    const uint32_t dt_ms = uint32_t(dt_seconds * 1000.0f + 0.5f);
    (mim::Line() << "tick " << ticks << " at " << abs_time_ms << " ms, dt " << dt_ms << " ms").send(mim::Level::info);
    return ticks < 3 ? mim::Tick::keep_going : mim::Tick::quit;
}

void app_shutdown() {
    (mim::Line() << "bye after " << ticks << " ticks").send(mim::Level::info);
}
}
