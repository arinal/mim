function(mim_add_test name)
    add_executable(${name} ${name}.c)
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    if(MIM_VALGRIND)
        add_test(NAME ${name} COMMAND valgrind --error-exitcode=1 --leak-check=full
                 --errors-for-leak-kinds=all $<TARGET_FILE:${name}>)
    else()
        add_test(NAME ${name} COMMAND ${name})
    endif()
endfunction()

mim_add_test(test_smoke)
