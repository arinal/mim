# SPDX-License-Identifier: MIT OR GPL-2.0-only

find_package(Threads REQUIRED)

add_library(mim_pl STATIC pl_linux.c)
target_include_directories(mim_pl PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(mim_pl PUBLIC Threads::Threads)
