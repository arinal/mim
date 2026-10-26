//===-- wrong_import.c - Imports mim.log with one arg ---------------------===//
//
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
//===----------------------------------------------------------------------===//

// Doesn't use evil.h or common.c: their mim_log has the right signature

#include <stdint.h>

__attribute__((import_module("mim"), import_name("log"))) void mim_log(int32_t level);

void app_init(void) {
    mim_log(1);
}

int32_t app_tick(uint64_t abs_time_ms, float dt_seconds) {
    (void)abs_time_ms;
    (void)dt_seconds;
    return 1;
}

void app_shutdown(void) {
}
