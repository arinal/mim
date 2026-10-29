//===-- cv_save_deep.c - Saves the canvas state 33 times ------------------===//
//
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
//===----------------------------------------------------------------------===//

#include "evil.h"

void app_init(void) {
    for (int i = 0; i < 33; i++) {
        mim_cv_save();
    }
}
