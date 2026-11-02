//===-- log_long.c - Logs 300 bytes ---------------------------------------===//
//
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
//===----------------------------------------------------------------------===//

#include "evil.h"

void app_init(void) {
    char msg[300];
    for (size_t i = 0; i < sizeof(msg); i++) {
        msg[i] = 'x';
    }
    mim_log(1, msg, sizeof(msg));
}
