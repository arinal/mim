# SPDX-License-Identifier: MIT OR GPL-2.0-only

add_library(mim_canvas STATIC canvas.c)
# no dependencies, so it brings its own path to the repo root for "runtime/canvas/canvas.h"
target_include_directories(mim_canvas PUBLIC ${PROJECT_SOURCE_DIR})
# ceilf
target_link_libraries(mim_canvas PRIVATE m)
