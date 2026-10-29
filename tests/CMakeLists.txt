# SPDX-License-Identifier: MIT OR GPL-2.0-only

if(MIM_VALGRIND)
    set(run_under valgrind --error-exitcode=1 --leak-check=full --errors-for-leak-kinds=all)
endif()

# stb_image and stb_image_write, for the tests that make or compare PNGs
set(stb_dir ${PROJECT_SOURCE_DIR}/extern/stb)

function(mim_add_test name)
    add_executable(${name} ${name}.c)
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(${name} PRIVATE mim_mem)
    add_test(NAME ${name} COMMAND ${run_under} $<TARGET_FILE:${name}>)
endfunction()

mim_add_test(test_smoke)
mim_add_test(test_pl_time)
mim_add_test(test_pl_log)
mim_add_test(test_pl_file)
mim_add_test(test_pl_thread)
mim_add_test(test_pl_atomic)
mim_add_test(test_pl_mem)
mim_add_test(test_pl_tick)
mim_add_test(test_mem_init)
mim_add_test(test_mem_bump)
mim_add_test(test_mem_heap)
mim_add_test(test_mem_death)
mim_add_test(test_canvas)
target_link_libraries(test_canvas PRIVATE mim_canvas m)
target_include_directories(test_canvas SYSTEM PRIVATE ${stb_dir})
# one scene per primitive against tests/golden/*.png; MIM_UPDATE_GOLDEN=1 rewrites them
mim_add_test(test_golden)
target_link_libraries(test_golden PRIVATE mim_canvas m)
target_include_directories(test_golden SYSTEM PRIVATE ${stb_dir})
target_compile_definitions(test_golden PRIVATE MIM_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden")
mim_add_test(test_loop)
target_link_libraries(test_loop PRIVATE mim_loop m)
mim_add_test(test_guest)
target_link_libraries(test_guest PRIVATE mim_guest mim_canvas mim_loop)
target_compile_definitions(test_guest PRIVATE MIM_GUEST_DIR="${MIM_GUEST_DIR}" MIM_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden")
target_include_directories(test_guest SYSTEM PRIVATE ${stb_dir})
add_dependencies(test_guest guests)

# SDL's dummy driver: a real window and renderer with no display or GPU. Not under valgrind:
# SDL's D-Bus setup leaves "still reachable" blocks, which --errors-for-leak-kinds=all counts.
if(NOT MIM_VALGRIND)
    # the test pushes SDL events itself
    find_package(SDL3 REQUIRED CONFIG COMPONENTS SDL3)
    mim_add_test(test_pl_display)
    target_link_libraries(test_pl_display PRIVATE mim_pl_display SDL3::SDL3)
    set_tests_properties(test_pl_display PROPERTIES ENVIRONMENT
        "SDL_VIDEO_DRIVER=dummy;TSAN_OPTIONS=suppressions=${CMAKE_CURRENT_SOURCE_DIR}/tsan.supp")
endif()

# not a test: `bench_frame <app.wasm>` prints how long a frame takes
add_executable(bench_frame bench_frame.c)
target_include_directories(bench_frame PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bench_frame PRIVATE mim_guest mim_canvas)

add_test(NAME test_mim COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test_mim.sh ${MIM_GUEST_DIR} ${run_under} $<TARGET_FILE:mim>)
# a mim that ignores Ctrl-C would otherwise hang here
set_tests_properties(test_mim PROPERTIES TIMEOUT 60)

if(MIM_FUZZ)
    # libFuzzer provides main(); ctest runs a short session, CI a longer one
    add_executable(fuzz_mem fuzz_mem.c)
    target_include_directories(fuzz_mem PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(fuzz_mem PRIVATE MIM_FUZZ)
    target_compile_options(fuzz_mem PRIVATE -fsanitize=fuzzer)
    target_link_options(fuzz_mem PRIVATE -fsanitize=fuzzer)
    target_link_libraries(fuzz_mem PRIVATE mim_mem)
    add_test(NAME fuzz_mem COMMAND fuzz_mem -runs=10000)
    add_executable(fuzz_image fuzz_image.c)
    target_include_directories(fuzz_image PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(fuzz_image PRIVATE MIM_FUZZ)
    target_compile_options(fuzz_image PRIVATE -fsanitize=fuzzer)
    target_link_options(fuzz_image PRIVATE -fsanitize=fuzzer)
    target_link_libraries(fuzz_image PRIVATE mim_canvas)
    # no corpus: libFuzzer would add what it finds to tests/fuzz_seeds; CI's longer run uses a copy
    add_test(NAME fuzz_image COMMAND fuzz_image -runs=10000)
else()
    # the same harnesses, fed by a fixed-seed pseudo-random generator
    mim_add_test(fuzz_mem)
    mim_add_test(fuzz_image)
    target_link_libraries(fuzz_image PRIVATE mim_canvas)
    target_include_directories(fuzz_image SYSTEM PRIVATE ${stb_dir})
endif()
