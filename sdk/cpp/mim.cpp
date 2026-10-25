//===-- mim.cpp - The C++ guest runtime: allocator and mem* functions -----===//
//
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
//===----------------------------------------------------------------------===//

#include "mim.h"

// set by wasm-ld: the first byte after the stack and the data
extern "C" unsigned char __heap_base;

namespace {

constexpr size_t page_size = 65536;

// ponytail: a bump allocator, delete does nothing. Fine for a guest that only
// allocates in app_init; a C++ UI that frees needs a free list.
size_t next;

void * bump(size_t size) {
    if (next == 0) {
        next = reinterpret_cast<size_t>(&__heap_base);
    }
    const size_t start = (next + 15) & ~size_t(15);
    if (size > SIZE_MAX - start) {
        __builtin_trap();
    }
    const size_t end = start + size;
    const size_t have = __builtin_wasm_memory_size(0) * page_size;
    if (end > have) {
        // the memory.grow instruction: the host says yes, or no with -1
        if (__builtin_wasm_memory_grow(0, (end - have + page_size - 1) / page_size) == SIZE_MAX) {
            mim::log(mim::Level::error, "out of memory");
            __builtin_trap();
        }
    }
    next = end;
    return reinterpret_cast<void *>(start);
}

} // namespace

void * operator new(size_t size) {
    return bump(size);
}

void * operator new[](size_t size) {
    return bump(size);
}

void operator delete(void *) noexcept {
}

void operator delete[](void *) noexcept {
}

void operator delete(void *, size_t) noexcept {
}

void operator delete[](void *, size_t) noexcept {
}

// clang turns loops into calls to these, even with -nostdlib
extern "C" {

void * memset(void * dst, int c, size_t n) {
    unsigned char * d = static_cast<unsigned char *>(dst);
    while (n-- > 0) {
        *d++ = static_cast<unsigned char>(c);
    }
    return dst;
}

void * memcpy(void * dst, const void * src, size_t n) {
    unsigned char * d = static_cast<unsigned char *>(dst);
    const unsigned char * s = static_cast<const unsigned char *>(src);
    while (n-- > 0) {
        *d++ = *s++;
    }
    return dst;
}

void * memmove(void * dst, const void * src, size_t n) {
    unsigned char * d = static_cast<unsigned char *>(dst);
    const unsigned char * s = static_cast<const unsigned char *>(src);
    if (d < s) {
        return memcpy(dst, src, n);
    }
    while (n-- > 0) {
        d[n] = s[n];
    }
    return dst;
}
}
