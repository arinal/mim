//===-- log_edge.c - Logs the last bytes of memory, then one past ---------===//
//
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
//===----------------------------------------------------------------------===//

#include "evil.h"

void app_init(void) {
    char *const end = (char *)(__builtin_wasm_memory_size(0) * 65536);
    static const char edge[] = "edge";
    for (int i = 0; i < 4; i++) {
        end[i - 4] = edge[i];
    }
    // the last 4 bytes: allowed
    mim_log(1, end - 4, 4);
    // the same length, one byte further: its last byte is outside
    mim_log(1, end - 3, 4);
}
