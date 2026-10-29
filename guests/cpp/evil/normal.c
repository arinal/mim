//===-- normal.c - A well-behaved app_init --------------------------------===//
//
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
//===----------------------------------------------------------------------===//

// For the cases that differ only in how they're linked (huge_memory, imported_memory)

#include "evil.h"

void app_init(void) {
    LOG("init");
}
