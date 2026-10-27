//===-- present_bad_pointer.c - A frame that runs past memory -------------===//
//
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
//===----------------------------------------------------------------------===//

#include "evil.h"

void app_init(void) {
    // starts 100 bytes before the end of linear memory
    mim_present((const uint32_t *)(__builtin_wasm_memory_size(0) * 65536 - 100), 1280, 720);
}
