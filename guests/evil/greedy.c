//===-- greedy.c - Takes all memory it can get ----------------------------===//
//
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
//===----------------------------------------------------------------------===//

#include "evil.h"

// Grows one page at a time until refused; each new page must be zero, then gets dirtied
void app_init(void) {
    uint32_t pages = 0;
    int all_zero = 1;
    for (;;) {
        const size_t old = __builtin_wasm_memory_grow(0, 1);
        if (old == SIZE_MAX) {
            break;
        }
        uint64_t * const page = (uint64_t *)(old * 65536);
        for (size_t i = 0; i < 65536 / 8; i++) {
            all_zero &= (page[i] == 0);
            page[i] = UINT64_MAX;
        }
        pages++;
    }
    char zero[] = "grew 0000 pages, all zero";
    char dirty[] = "grew 0000 pages, some not zero";
    char * const msg = all_zero ? zero : dirty;
    for (int i = 8; i >= 5; i--) {
        msg[i] = (char)('0' + pages % 10);
        pages /= 10;
    }
    mim_log(1, msg, all_zero ? sizeof(zero) - 1 : sizeof(dirty) - 1);
}
