# SPDX-License-Identifier: MIT OR GPL-2.0-only

# Builds every guest into ${MIM_GUEST_DIR}. The host may be built with GCC, so the C/C++
# guests find clang themselves; it needs the wasm32 target and wasm-ld (lld).
find_program(MIM_WASM_CLANG clang REQUIRED)
find_program(MIM_CARGO cargo REQUIRED)

# WASM 1.0 only (clang's default CPU adds bulk memory and more); a 1 MiB stack, as Rust has
set(wasm_flags --target=wasm32 -mcpu=mvp -nostdlib -O2 -Wall -Wextra -Werror
    -Wl,--no-entry -Wl,-z,stack-size=1048576
    -Wl,--export-if-defined=app_init -Wl,--export-if-defined=app_tick -Wl,--export-if-defined=app_shutdown)

# C++
set(cpp_sdk ${PROJECT_SOURCE_DIR}/sdk/cpp)
set(cpp_sources ${cpp_sdk}/mim.cpp ${CMAKE_CURRENT_SOURCE_DIR}/hello_cpp/hello.cpp)
add_custom_command(
    OUTPUT ${MIM_GUEST_DIR}/hello_cpp.wasm
    COMMAND ${MIM_WASM_CLANG} -x c++ -std=c++17 -fno-exceptions -fno-rtti ${wasm_flags} -I${cpp_sdk} -o ${MIM_GUEST_DIR}/hello_cpp.wasm ${cpp_sources}
    DEPENDS ${cpp_sources} ${cpp_sdk}/mim.h
    VERBATIM)
set(outputs ${MIM_GUEST_DIR}/hello_cpp.wasm)

# Hostile guests: one per case, from <case>.c and common.c
set(evil_dir ${CMAKE_CURRENT_SOURCE_DIR}/evil)
set(evil_cases bad_pointer wrapping_pointer bad_level log_injection greedy recursion
    missing_export wrong_signature wrong_import unknown_import huge_memory imported_memory tick_trap)
# these replace or drop common.c's exports
set(evil_missing_export_sources missing_export.c)
set(evil_wrong_signature_sources wrong_signature.c)
set(evil_wrong_import_sources wrong_import.c)
set(evil_tick_trap_sources tick_trap.c)
# these differ only in how they're linked
set(evil_huge_memory_sources normal.c common.c)
set(evil_huge_memory_flags -Wl,--initial-memory=67108864)
set(evil_imported_memory_sources normal.c common.c)
set(evil_imported_memory_flags -Wl,--import-memory)
foreach(case IN LISTS evil_cases)
    if(NOT DEFINED evil_${case}_sources)
        set(evil_${case}_sources ${case}.c common.c)
    endif()
    list(TRANSFORM evil_${case}_sources PREPEND ${evil_dir}/)
    set(out ${MIM_GUEST_DIR}/evil_${case}.wasm)
    add_custom_command(
        OUTPUT ${out}
        COMMAND ${MIM_WASM_CLANG} -std=c99 ${wasm_flags} ${evil_${case}_flags} -o ${out} ${evil_${case}_sources}
        DEPENDS ${evil_${case}_sources} ${evil_dir}/evil.h
        VERBATIM)
    list(APPEND outputs ${out})
endforeach()

# Rust: cargo runs every build and decides itself whether anything changed
set(cargo_dir ${CMAKE_CURRENT_BINARY_DIR}/cargo)
add_custom_target(guest_rust ALL
    COMMAND ${MIM_CARGO} build --quiet --release --target wasm32v1-none --target-dir ${cargo_dir}
        --manifest-path ${PROJECT_SOURCE_DIR}/Cargo.toml
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${cargo_dir}/wasm32v1-none/release/hello.wasm ${MIM_GUEST_DIR}/hello_rust.wasm
    BYPRODUCTS ${MIM_GUEST_DIR}/hello_rust.wasm
    VERBATIM)

add_custom_target(guests ALL DEPENDS ${outputs} guest_rust)
