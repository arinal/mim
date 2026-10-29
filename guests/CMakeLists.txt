# SPDX-License-Identifier: MIT OR GPL-2.0-only

# Builds every guest into ${MIM_GUEST_DIR}. The host may be built with GCC, so the C/C++
# guests find clang themselves; it needs the wasm32 target and wasm-ld (lld). Apple's clang has
# no wasm32 backend, so on macOS Homebrew's llvm comes first.
find_program(MIM_WASM_CLANG clang HINTS /opt/homebrew/opt/llvm/bin /usr/local/opt/llvm/bin REQUIRED)
find_program(MIM_CARGO cargo REQUIRED)

# WASM 1.0 only (clang's default CPU adds bulk memory and more); a 1 MiB stack, as Rust has
set(wasm_flags --target=wasm32 -mcpu=mvp -nostdlib -O2 -Wall -Wextra -Werror
    -Wl,--no-entry -Wl,-z,stack-size=1048576
    -Wl,--export-if-defined=app_init -Wl,--export-if-defined=app_tick -Wl,--export-if-defined=app_shutdown)
set(wasm_cflags ${wasm_flags})
list(FILTER wasm_cflags EXCLUDE REGEX "^-Wl,")

# CMake leaves custom commands out of compile_commands.json, so the guests' sources are listed here
# for clangd; merge_compile_commands.cmake adds them to it on every build
set(db "")
macro(guest_db flags)
    foreach(src IN ITEMS ${ARGN})
        set(args ${MIM_WASM_CLANG} ${flags} -c ${src})
        list(TRANSFORM args PREPEND "\"")
        list(TRANSFORM args APPEND "\"")
        list(JOIN args ", " args)
        list(APPEND db "{\"directory\": \"${CMAKE_CURRENT_BINARY_DIR}\", \"file\": \"${src}\", \"arguments\": [${args}]}")
    endforeach()
endmacro()

# C++
set(cpp_sdk ${PROJECT_SOURCE_DIR}/sdk/cpp)
set(cpp_sources ${cpp_sdk}/mim.cpp ${CMAKE_CURRENT_SOURCE_DIR}/cpp/hello.cpp)
add_custom_command(
    OUTPUT ${MIM_GUEST_DIR}/hello_cpp.wasm
    COMMAND ${MIM_WASM_CLANG} -x c++ -std=c++17 -fno-exceptions -fno-rtti ${wasm_flags} -I${cpp_sdk} -o ${MIM_GUEST_DIR}/hello_cpp.wasm ${cpp_sources}
    DEPENDS ${cpp_sources} ${cpp_sdk}/mim.h ${cpp_sdk}/ffi.h
    VERBATIM)
set(outputs ${MIM_GUEST_DIR}/hello_cpp.wasm)
guest_db("-x;c++;-std=c++17;-fno-exceptions;-fno-rtti;${wasm_cflags};-I${cpp_sdk}" ${cpp_sources})

# The canvas demo, with its poster built in as a C array (Rust uses include_bytes!)
set(poster ${CMAKE_CURRENT_SOURCE_DIR}/assets/poster.png)
set(poster_h ${CMAKE_CURRENT_BINARY_DIR}/poster_png.h)
add_custom_command(
    OUTPUT ${poster_h}
    COMMAND ${CMAKE_COMMAND} -DIN=${poster} -DOUT=${poster_h} -DNAME=poster_png -P ${PROJECT_SOURCE_DIR}/cmake/embed.cmake
    DEPENDS ${poster} ${PROJECT_SOURCE_DIR}/cmake/embed.cmake
    VERBATIM)
set(canvas_sources ${cpp_sdk}/mim.cpp ${CMAKE_CURRENT_SOURCE_DIR}/cpp/canvas.cpp)
add_custom_command(
    OUTPUT ${MIM_GUEST_DIR}/canvas_cpp.wasm
    COMMAND ${MIM_WASM_CLANG} -x c++ -std=c++17 -fno-exceptions -fno-rtti ${wasm_flags} -I${cpp_sdk} -I${CMAKE_CURRENT_BINARY_DIR} -o ${MIM_GUEST_DIR}/canvas_cpp.wasm ${canvas_sources}
    DEPENDS ${canvas_sources} ${cpp_sdk}/mim.h ${cpp_sdk}/ffi.h ${poster_h}
    VERBATIM)
list(APPEND outputs ${MIM_GUEST_DIR}/canvas_cpp.wasm)
# mim.cpp is already listed with hello_cpp
guest_db("-x;c++;-std=c++17;-fno-exceptions;-fno-rtti;${wasm_cflags};-I${cpp_sdk};-I${CMAKE_CURRENT_BINARY_DIR}"
    ${CMAKE_CURRENT_SOURCE_DIR}/cpp/canvas.cpp)

# The app UI, the same app as guests/rust/ui
set(ui_sources ${cpp_sdk}/mim.cpp ${CMAKE_CURRENT_SOURCE_DIR}/cpp/ui.cpp)
add_custom_command(
    OUTPUT ${MIM_GUEST_DIR}/ui_cpp.wasm
    COMMAND ${MIM_WASM_CLANG} -x c++ -std=c++17 -fno-exceptions -fno-rtti ${wasm_flags} -I${cpp_sdk} -o ${MIM_GUEST_DIR}/ui_cpp.wasm ${ui_sources}
    DEPENDS ${ui_sources} ${cpp_sdk}/mim.h ${cpp_sdk}/ffi.h
    VERBATIM)
list(APPEND outputs ${MIM_GUEST_DIR}/ui_cpp.wasm)
guest_db("-x;c++;-std=c++17;-fno-exceptions;-fno-rtti;${wasm_cflags};-I${cpp_sdk}" ${CMAKE_CURRENT_SOURCE_DIR}/cpp/ui.cpp)

# Hostile guests: one per case, from <case>.c and common.c
set(evil_dir ${CMAKE_CURRENT_SOURCE_DIR}/cpp/evil)
set(evil_cases bad_pointer wrapping_pointer log_edge huge_len bad_level log_injection log_long greedy recursion
    missing_export wrong_signature wrong_types wrong_import unknown_import huge_memory imported_memory tick_trap
    cv_save_deep cv_path_full cv_bad_image cv_negative_arc cv_bad_png read_events_edge read_events_huge_cap)
# these replace or drop common.c's exports
set(evil_missing_export_sources missing_export.c)
set(evil_wrong_signature_sources wrong_signature.c)
set(evil_wrong_types_sources wrong_types.c)
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
        COMMAND ${MIM_WASM_CLANG} -std=c99 ${wasm_flags} -I${cpp_sdk} ${evil_${case}_flags} -o ${out} ${evil_${case}_sources}
        DEPENDS ${evil_${case}_sources} ${evil_dir}/evil.h ${cpp_sdk}/ffi.h
        VERBATIM)
    list(APPEND outputs ${out})
    list(APPEND evil_sources ${evil_${case}_sources})
endforeach()
# every case compiles with the same flags; only the linking differs
list(REMOVE_DUPLICATES evil_sources)
guest_db("-std=c99;${wasm_cflags};-I${cpp_sdk}" ${evil_sources})

list(JOIN db ",\n" db)
file(GENERATE OUTPUT ${CMAKE_BINARY_DIR}/compile_commands_guests.json CONTENT "[\n${db}\n]\n")
# poster_png.h too, so clangd finds canvas.cpp's include without building the guests
add_custom_target(compile_commands ALL
    COMMAND ${CMAKE_COMMAND} -DDB=${CMAKE_BINARY_DIR}/compile_commands.json
        -DGUESTS=${CMAKE_BINARY_DIR}/compile_commands_guests.json -P ${CMAKE_CURRENT_SOURCE_DIR}/merge_compile_commands.cmake
    DEPENDS ${poster_h}
    VERBATIM)

# Rust: cargo runs every build and decides itself whether anything changed
set(cargo_dir ${CMAKE_CURRENT_BINARY_DIR}/cargo)
add_custom_target(guest_rust ALL
    COMMAND ${MIM_CARGO} build --quiet --release --target wasm32v1-none --target-dir ${cargo_dir}
        --manifest-path ${PROJECT_SOURCE_DIR}/Cargo.toml
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${cargo_dir}/wasm32v1-none/release/hello.wasm ${MIM_GUEST_DIR}/hello_rust.wasm
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${cargo_dir}/wasm32v1-none/release/bounce.wasm ${MIM_GUEST_DIR}/bounce_rust.wasm
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${cargo_dir}/wasm32v1-none/release/canvas.wasm ${MIM_GUEST_DIR}/canvas_rust.wasm
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${cargo_dir}/wasm32v1-none/release/ui.wasm ${MIM_GUEST_DIR}/ui_rust.wasm
    BYPRODUCTS ${MIM_GUEST_DIR}/hello_rust.wasm ${MIM_GUEST_DIR}/bounce_rust.wasm ${MIM_GUEST_DIR}/canvas_rust.wasm
        ${MIM_GUEST_DIR}/ui_rust.wasm
    VERBATIM)

add_custom_target(guests ALL DEPENDS ${outputs} guest_rust)
