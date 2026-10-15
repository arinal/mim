//===-- evil.c - Guests that attack the host, one per -DEVIL_<CASE> -------===//
//
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// Each case must be rejected at load or trap; none may take the host down.
//
//===----------------------------------------------------------------------===//

#include <stddef.h>
#include <stdint.h>

#define IMPORT(_name) __attribute__((import_module("mim"), import_name(#_name)))

#ifdef EVIL_WRONG_IMPORT
IMPORT(log)
void mim_log(int32_t level);
#define LOG(_s) mim_log(1)
#else
IMPORT(log)
void mim_log(int32_t level, const char * ptr, size_t len);
#define LOG(_s) mim_log(1, _s, sizeof(_s) - 1)
#endif

#ifdef EVIL_RECURSION
// called through a volatile pointer, so clang can't turn the recursion into a loop
static int deep(int n);
static int (*volatile deep_ptr)(int) = deep;
static int deep(int n) {
    return deep_ptr(n + 1) + n;
}
#endif

#ifdef EVIL_UNKNOWN_IMPORT
IMPORT(nope)
void nope(void);
#endif

#ifdef EVIL_GREEDY
// Grows one page at a time until refused; each new page must be zero, then gets dirtied
static void greedy(void) {
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
#endif

void * memset(void * dst, int c, size_t n) {
    unsigned char * d = dst;
    while (n-- > 0) {
        *d++ = (unsigned char)c;
    }
    return dst;
}

void app_init(void) {
#if defined(EVIL_BAD_POINTER)
    mim_log(1, (const char *)0x7FFFFFF0, 16);
#elif defined(EVIL_WRAPPING_POINTER)
    // in 32 bits, ptr + len wraps around to 0x10
    mim_log(1, (const char *)0xFFFFFFF0, 0x20);
#elif defined(EVIL_BAD_LEVEL)
    mim_log(7, "x", 1);
#elif defined(EVIL_LOG_INJECTION)
    LOG("ok\nE 1.000 fake");
#elif defined(EVIL_GREEDY)
    greedy();
#elif defined(EVIL_RECURSION)
    deep(0);
#elif defined(EVIL_WRONG_IMPORT)
    mim_log(1);
#elif defined(EVIL_UNKNOWN_IMPORT)
    nope();
#else
    LOG("init");
#endif
}

#ifdef EVIL_WRONG_SIGNATURE
void app_tick(int32_t x) {
    (void)x;
}
#elif !defined(EVIL_MISSING_EXPORT)
void app_tick(uint64_t abs_time_ms, float dt_seconds) {
    (void)abs_time_ms;
    (void)dt_seconds;
    LOG("tick");
}
#endif

void app_shutdown(void) {
    LOG("bye");
}
