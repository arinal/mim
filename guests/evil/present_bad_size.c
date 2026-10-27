//===-- present_bad_size.c - A 640x480 frame ------------------------------===//
//
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
//===----------------------------------------------------------------------===//

#include "evil.h"

void app_init(void) {
    static uint32_t pixels[640 * 480];
    mim_present(pixels, 640, 480);
}
