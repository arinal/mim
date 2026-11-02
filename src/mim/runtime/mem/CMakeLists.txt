# SPDX-License-Identifier: MIT OR GPL-2.0-only

add_library(mim_mem STATIC mem.c)
target_link_libraries(mim_mem PUBLIC mim_pl)

if(MIM_FUZZ)
    # Coverage instrumentation for the allocator's branches. Without it libFuzzer only sees the
    # harness's own branches and mutates blindly.
    target_compile_options(mim_mem PRIVATE -fsanitize=fuzzer-no-link)
endif()
