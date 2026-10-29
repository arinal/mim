//===-- huge_len.c - Logs with a length bigger than all of memory ---------===//
//
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
//===----------------------------------------------------------------------===//

#include "evil.h"

void app_init(void) {
    // ptr is inside memory; a check written as ptr + len <= size wouldn't catch this
    const size_t size = __builtin_wasm_memory_size(0) * 65536;
    mim_log(1, (const char *)(size - 10), 0xFFFFFFFF);
}
