# SPDX-License-Identifier: MIT OR GPL-2.0-only

find_package(Threads REQUIRED)

add_library(mim_pl STATIC pl_posix.c)
# includes are by path from the repo root: "platform/pl.h", "runtime/mem/mem.h"
target_include_directories(mim_pl PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(mim_pl PUBLIC Threads::Threads)

# The window, separate so only programs that open one link SDL
find_package(SDL3 REQUIRED CONFIG COMPONENTS SDL3)
add_library(mim_pl_display STATIC pl_display_sdl.c)
target_link_libraries(mim_pl_display PUBLIC mim_pl PRIVATE SDL3::SDL3)
