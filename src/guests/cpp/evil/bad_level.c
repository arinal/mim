//===-- bad_level.c - Logs with an unknown level --------------------------===//
//
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
//===----------------------------------------------------------------------===//

#include "evil.h"

void app_init(void) {
    mim_log(7, "x", 1);
}
