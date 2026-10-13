# SPDX-License-Identifier: MIT OR GPL-2.0-only

function(mim_add_test name)
    add_executable(${name} ${name}.c)
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(${name} PRIVATE mim_pl)
    if(MIM_VALGRIND)
        add_test(NAME ${name} COMMAND valgrind --error-exitcode=1 --leak-check=full
                 --errors-for-leak-kinds=all $<TARGET_FILE:${name}>)
    else()
        add_test(NAME ${name} COMMAND ${name})
    endif()
endfunction()

mim_add_test(test_smoke)
mim_add_test(test_pl_time)
mim_add_test(test_pl_log)
mim_add_test(test_pl_file)
mim_add_test(test_pl_thread)
mim_add_test(test_pl_atomic)
