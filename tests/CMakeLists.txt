# SPDX-License-Identifier: MIT OR GPL-2.0-only

if(MIM_VALGRIND)
    set(run_under valgrind --error-exitcode=1 --leak-check=full --errors-for-leak-kinds=all)
endif()

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
mim_add_test(test_loop)
target_link_libraries(test_loop PRIVATE mim_loop m)
mim_add_test(test_guest)
target_link_libraries(test_guest PRIVATE mim_guest mim_loop)
target_compile_definitions(test_guest PRIVATE MIM_GUEST_DIR="${MIM_GUEST_DIR}")
add_dependencies(test_guest guests)

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
else()
    # the same harness, fed by a fixed-seed pseudo-random generator
    mim_add_test(fuzz_mem)
endif()
