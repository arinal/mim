//===-- tick_trap.c - Traps in its first app_tick -------------------------===//
//
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
//===----------------------------------------------------------------------===//

// Doesn't use common.c: its app_tick doesn't trap, and its app_shutdown must not log here

#include "evil.h"

void app_init(void) {
    LOG("init");
}

int32_t app_tick(uint64_t abs_time_ms, float dt_seconds) {
    (void)abs_time_ms;
    (void)dt_seconds;
    __builtin_trap();
}

// never called: a dead guest doesn't get app_shutdown
void app_shutdown(void) {
    LOG("bye");
}
