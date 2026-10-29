//===-- cv_bad_png.c - A PNG that isn't one, then one past memory ---------===//
//
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
//===----------------------------------------------------------------------===//

#include "evil.h"

void app_init(void) {
    // a PNG signature and then nothing a decoder wants: refused, not a trap
    static const char junk[] = "\x89PNG\r\n\x1a\n\x00\x00\x00\x0dIHDR";
    if (mim_cv_image_load(junk, sizeof(junk)) == 0) {
        LOG("refused");
    }
    // the last 8 bytes of memory and 8 more
    mim_cv_image_load((const void *)(__builtin_wasm_memory_size(0) * 65536 - 8), 16);
}
