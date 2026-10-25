//===-- common.c - The normal exports and memset --------------------------===//
//
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
//===----------------------------------------------------------------------===//

#include "evil.h"

// clang emits calls to it, and there's no libc
void * memset(void * dst, int c, size_t n) {
    unsigned char * d = dst;
    while (n-- > 0) {
        *d++ = (unsigned char)c;
    }
    return dst;
}

void app_tick(uint64_t abs_time_ms, float dt_seconds) {
    (void)abs_time_ms;
    (void)dt_seconds;
    LOG("tick");
}

void app_shutdown(void) {
    LOG("bye");
}
