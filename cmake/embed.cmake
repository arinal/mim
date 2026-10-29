# SPDX-License-Identifier: MIT OR GPL-2.0-only
#
# Writes a file's bytes as a C array, for data built into a binary:
#   cmake -DIN=<file> -DOUT=<file.c or .h> -DNAME=<symbol> -P embed.cmake
# gives `const unsigned char <NAME>[]` and `const unsigned int <NAME>_size`.

file(READ ${IN} hex HEX)
string(LENGTH "${hex}" length)
math(EXPR size "${length} / 2")
# 16 bytes a line
string(REGEX REPLACE "(................................)" "\\1\n" hex "${hex}")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," hex "${hex}")
get_filename_component(source ${IN} NAME)
file(WRITE ${OUT} "// GENERATED from ${source} by cmake/embed.cmake: don't edit\n\n"
    "const unsigned char ${NAME}[] = {\n${hex}};\n"
    "const unsigned int ${NAME}_size = ${size};\n")
