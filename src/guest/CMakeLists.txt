# SPDX-License-Identifier: MIT OR GPL-2.0-only

# wasm3, vendored in extern/wasm3: only the interpreter, no WASI or libc bindings
set(wasm3_dir ${PROJECT_SOURCE_DIR}/extern/wasm3/source)
add_library(wasm3 STATIC
    ${wasm3_dir}/m3_bind.c ${wasm3_dir}/m3_code.c ${wasm3_dir}/m3_compile.c ${wasm3_dir}/m3_core.c
    ${wasm3_dir}/m3_env.c ${wasm3_dir}/m3_exec.c ${wasm3_dir}/m3_function.c ${wasm3_dir}/m3_info.c
    ${wasm3_dir}/m3_module.c ${wasm3_dir}/m3_parse.c ${wasm3_dir}/m3_validate.c)
target_include_directories(wasm3 PUBLIC ${wasm3_dir})
# ceilf, sqrt, ... for the float instructions
target_link_libraries(wasm3 PUBLIC m)
# allocations go to guest.c's hooks instead of calloc/realloc/free
target_compile_definitions(wasm3 PUBLIC d_m3HostAllocator=1)
# not our warnings to fix, and -Werror would break on the next compiler upgrade
target_compile_options(wasm3 PRIVATE -w)
set_target_properties(wasm3 PROPERTIES C_STANDARD 11)
if(MIM_SANITIZER)
    # wasm3 calls callbacks through a generic function pointer type (ForEachModule): undefined
    # behaviour that clang's UBSan reports, harmless on every ABI MIM targets
    target_compile_options(wasm3 PRIVATE $<$<C_COMPILER_ID:Clang>:-fno-sanitize=function>)
endif()

add_library(mim_guest STATIC guest.c)
target_link_libraries(mim_guest PUBLIC mim_mem PRIVATE wasm3)
