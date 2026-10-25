//===-- wrapping_pointer.c - Logs with ptr + len wrapping -----------------===//
//
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
//===----------------------------------------------------------------------===//

#include "evil.h"

void app_init(void) {
    // in 32 bits, ptr + len wraps around to 0x10
    mim_log(1, (const char *)0xFFFFFFF0, 0x20);
}
