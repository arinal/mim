//===-- wrong_signature.c - Exports app_tick(i32) -------------------------===//
//
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
//===----------------------------------------------------------------------===//

// Doesn't use common.c: its app_tick has the right signature

#include "evil.h"

void app_init(void) {
    LOG("init");
}

void app_tick(int32_t x) {
    (void)x;
}

void app_shutdown(void) {
}
