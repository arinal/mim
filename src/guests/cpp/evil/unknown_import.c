//===-- unknown_import.c - Imports what the host lacks --------------------===//
//
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
//===----------------------------------------------------------------------===//

#include "evil.h"

IMPORT(nope)
void nope(void);

void app_init(void) {
    nope();
}
