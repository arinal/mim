//===-- read_events_edge.c - Events at the end of memory, then past -------===//
//
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
//===----------------------------------------------------------------------===//

#include "evil.h"

void app_init(void) {
    // room for exactly one event in the last 8 bytes: fine, and empty outside app_tick
    void *const last = (void *)(__builtin_wasm_memory_size(0) * 65536 - mim_event_size);
    if (mim_read_events(last, 1) == 0) {
        LOG("edge");
    }
    // the same place with room for two traps, though there's nothing to copy
    mim_read_events(last, 2);
}
