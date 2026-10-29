//===-- read_events_huge_cap.c - A cap whose size wraps in 32 bits --------===//
//
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
//===----------------------------------------------------------------------===//

#include "evil.h"

void app_init(void) {
    // 2^29 events of 8 bytes is 2^32: 0, if the host multiplied in 32 bits
    static uint32_t buf[2];
    mim_read_events(buf, 0x20000000);
}
