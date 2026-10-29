//===-- recursion.c - Overflows the stack ---------------------------------===//
//
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
//===----------------------------------------------------------------------===//

#include "evil.h"

// called through a volatile pointer, so clang can't turn the recursion into a loop
static int deep(int n);
static int (*volatile deep_ptr)(int) = deep;
static int deep(int n) {
    return deep_ptr(n + 1) + n;
}

void app_init(void) {
    deep(0);
}
