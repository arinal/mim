//===-- missing_export.c - Exports no app_tick ----------------------------===//
//
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
//===----------------------------------------------------------------------===//

// Doesn't use common.c: it would export app_tick

#include "evil.h"

void app_init(void) {
    LOG("init");
}

void app_shutdown(void) {
}
