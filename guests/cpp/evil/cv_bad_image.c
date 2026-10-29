//===-- cv_bad_image.c - Draws an image it never loaded -------------------===//
//
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
//===----------------------------------------------------------------------===//

#include "evil.h"

void app_init(void) {
    mim_cv_draw_image(7, 0, 0, 10, 10);
}
