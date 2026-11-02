//===-- cv_negative_arc.c - An arc with a negative radius -----------------===//
//
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
//===----------------------------------------------------------------------===//

#include "evil.h"

void app_init(void) {
    mim_cv_arc(10, 10, -1, 0, 1, 0);
}
