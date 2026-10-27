//===-- present_wrapping.c - A frame with ptr + len wrapping --------------===//
//
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
//===----------------------------------------------------------------------===//

#include "evil.h"

void app_init(void) {
    // in 32 bits, ptr + 1280 * 720 * 4 wraps around to 0x100: inside memory, so only a check
    // that can't overflow rejects it
    mim_present((const uint32_t *)(0u - 1280u * 720u * 4u + 0x100u), 1280, 720);
}
