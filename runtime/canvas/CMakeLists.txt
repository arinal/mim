# SPDX-License-Identifier: MIT OR GPL-2.0-only

# the built-in font, as a C array
set(font ${PROJECT_SOURCE_DIR}/extern/fonts/LiberationSans-Regular.ttf)
set(font_c ${CMAKE_CURRENT_BINARY_DIR}/font_data.c)
add_custom_command(
    OUTPUT ${font_c}
    COMMAND ${CMAKE_COMMAND} -DIN=${font} -DOUT=${font_c} -DNAME=cv_font_data -P ${PROJECT_SOURCE_DIR}/cmake/embed.cmake
    DEPENDS ${font} ${PROJECT_SOURCE_DIR}/cmake/embed.cmake
    VERBATIM)

add_library(mim_canvas STATIC canvas.c raster.c image.c text.c ${font_c})
target_link_libraries(mim_canvas PUBLIC mim_mem PRIVATE m)
# stb's headers are someone else's code: their warnings aren't ours to fix
target_include_directories(mim_canvas SYSTEM PRIVATE ${PROJECT_SOURCE_DIR}/extern/stb)

if(MIM_FUZZ)
    # coverage for stb_image's branches, so fuzz_image's inputs aren't blind
    target_compile_options(mim_canvas PRIVATE -fsanitize=fuzzer-no-link)
endif()
