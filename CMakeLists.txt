cmake_minimum_required(VERSION 3.20)
project(mim C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(MIM_SANITIZE "Build with AddressSanitizer + UndefinedBehaviorSanitizer" OFF)
option(MIM_VALGRIND "Run tests under valgrind" OFF)

add_compile_options(-Wall -Wextra -Werror -pedantic)

if(MIM_SANITIZE)
    set(san -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
    add_compile_options(${san})
    add_link_options(${san})
endif()

enable_testing()
add_subdirectory(tests)
