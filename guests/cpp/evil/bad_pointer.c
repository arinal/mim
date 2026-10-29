//===-- bad_pointer.c - Logs from outside memory --------------------------===//
//
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
//===----------------------------------------------------------------------===//

#include "evil.h"

void app_init(void) {
    mim_log(1, (const char *)0x7FFFFFF0, 16);
}
