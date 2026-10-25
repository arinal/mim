//===-- log_injection.c - Fakes a second log line -------------------------===//
//
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
//===----------------------------------------------------------------------===//

#include "evil.h"

void app_init(void) {
    LOG("ok\nE 1.000 fake");
}
