//===-- golden.h - Comparing a frame with tests/golden/<name>.png ---------===//
//
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// Include it in one file of a test; it brings its own static copy of
// stb_image and stb_image_write, which can't clash with the canvas's.
// MIM_GOLDEN_DIR must name tests/golden.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "mim_test.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#pragma GCC diagnostic ignored "-Wunused-function"
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "stb_image.h"
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

// How far a channel may be off: compilers may round floats a little differently
static const int golden_tolerance = 2;

// Checks `pixels` (width × height of 0x00RRGGBB) against tests/golden/<name>.png. On a mismatch
// it says where, writes the frame to <name>.actual.png in the working directory and returns false.
// With MIM_UPDATE_GOLDEN=1 it rewrites the golden instead: look at it before committing it.
static bool golden_check(const char *const name, const uint32_t *const pixels, const int width, const int height) {
    const size_t count = (size_t)width * (size_t)height;
    unsigned char *const rgba = malloc(count * 4);
    CHECK(rgba != NULL);
    for (size_t i = 0; i < count; i++) {
        rgba[i * 4 + 0] = (unsigned char)(pixels[i] >> 16);
        rgba[i * 4 + 1] = (unsigned char)(pixels[i] >> 8);
        rgba[i * 4 + 2] = (unsigned char)pixels[i];
        rgba[i * 4 + 3] = 255;
    }
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.png", MIM_GOLDEN_DIR, name);
    const char *const update = getenv("MIM_UPDATE_GOLDEN");
    if (update != NULL && strcmp(update, "1") == 0) {
        CHECK(stbi_write_png(path, width, height, 4, rgba, width * 4));
        fprintf(stderr, "wrote %s\n", path);
        free(rgba);
        return true;
    }

    int gw, gh, channels;
    unsigned char *const golden = stbi_load(path, &gw, &gh, &channels, 4);
    size_t bad = 0;
    int worst = 0;
    size_t first = 0;
    if (golden == NULL || gw != width || gh != height) {
        fprintf(stderr, "%s: no %dx%d golden at %s\n", name, width, height, path);
        bad = 1;
    } else {
        for (size_t i = 0; i < count * 4; i++) {
            const int d = abs((int)rgba[i] - (int)golden[i]);
            if (d > golden_tolerance) {
                first = bad == 0 ? i / 4 : first;
                bad++;
            }
            worst = d > worst ? d : worst;
        }
        if (bad != 0) {
            fprintf(stderr, "%s: %zu channels more than %d off (worst %d), first at (%zu, %zu); see %s.actual.png\n", name, bad, golden_tolerance, worst, first % (size_t)width, first / (size_t)width, name);
        }
    }
    stbi_image_free(golden);
    if (bad != 0) {
        snprintf(path, sizeof(path), "%s.actual.png", name);
        CHECK(stbi_write_png(path, width, height, 4, rgba, width * 4));
    }
    free(rgba);
    return bad == 0;
}
