# SPDX-License-Identifier: MIT OR GPL-2.0-only

add_library(mim_loop STATIC loop.c)
# no dependencies, so it brings its own path to the repo root for "runtime/loop/loop.h"
target_include_directories(mim_loop PUBLIC ${PROJECT_SOURCE_DIR})
