//===-- cv_path_full.c - A path one point longer than the host takes ------===//
//
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
//===----------------------------------------------------------------------===//

#include "evil.h"

void app_init(void) {
    mim_cv_move_to(0, 0);
    for (int i = 1; i <= 16384; i++) {
        mim_cv_line_to((float)(i % 100), (float)(i % 7));
    }
}
