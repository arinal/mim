//===-- wrong_types.c - Exports app_tick(i64, i64) ------------------------===//
//
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
//===----------------------------------------------------------------------===//

// Doesn't use common.c: its app_tick has the right signature

#include "evil.h"

void app_init(void) {
    LOG("init");
}

// the right number of arguments, but dt is an i64 instead of an f32
int32_t app_tick(uint64_t abs_time_ms, uint64_t dt) {
    (void)abs_time_ms;
    (void)dt;
    return 1;
}

void app_shutdown(void) {
}
