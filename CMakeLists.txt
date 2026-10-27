# SPDX-License-Identifier: MIT OR GPL-2.0-only

cmake_minimum_required(VERSION 3.20)
project(mim C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(MIM_PLATFORM linux CACHE STRING "Platform layer to build, from platform/<platform>")
set(MIM_SANITIZER "" CACHE STRING "'' (none), 'address' (ASan + UBSan) or 'thread' (TSan + UBSan)")
option(MIM_VALGRIND "Run tests under valgrind" OFF)
option(MIM_FUZZ "Build fuzz_mem as a libFuzzer target (clang only)" OFF)

add_compile_options(-Wall -Wextra -Werror -pedantic)

if(MIM_SANITIZER)
    set(san -fsanitize=${MIM_SANITIZER},undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
    add_compile_options(${san})
    add_link_options(${san})
endif()

add_subdirectory(platform/${MIM_PLATFORM})
add_subdirectory(runtime/mem)
add_subdirectory(runtime/guest)
add_subdirectory(runtime/loop)

add_executable(mim runtime/main.c)
target_link_libraries(mim PRIVATE mim_guest mim_loop mim_pl_display)

set(MIM_GUEST_DIR ${CMAKE_BINARY_DIR}/guests)
file(MAKE_DIRECTORY ${MIM_GUEST_DIR})
add_subdirectory(guests)

enable_testing()
add_subdirectory(tests)
