//===-- mim_test.h - The test framework: one CHECK macro ------------------===//
//
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
//===----------------------------------------------------------------------===//

#pragma once

#include <stdio.h>
#include <stdlib.h>

// One test executable per file; the first failing CHECK fails the whole executable.
#define CHECK(_cond)                                                                  \
    do {                                                                              \
        if (!(_cond)) {                                                               \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #_cond); \
            exit(1);                                                                  \
        }                                                                             \
    } while (0)
