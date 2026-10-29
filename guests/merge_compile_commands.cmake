# SPDX-License-Identifier: MIT OR GPL-2.0-only

# cmake -DDB=<compile_commands.json> -DGUESTS=<compile_commands_guests.json> -P merge_compile_commands.cmake
# Adds GUESTS' entries to DB, replacing any earlier copy of them, so it can run on every build.
file(READ ${DB} db)
file(READ ${GUESTS} guests)

set(guest_files "")
set(entries "")
string(JSON n LENGTH "${guests}")
math(EXPR last "${n} - 1")
foreach(i RANGE ${last})
    string(JSON entry GET "${guests}" ${i})
    string(JSON file GET "${entry}" file)
    list(APPEND guest_files ${file})
    list(APPEND entries "${entry}")
endforeach()

set(host "")
string(JSON n LENGTH "${db}")
if(n GREATER 0)
    math(EXPR last "${n} - 1")
    foreach(i RANGE ${last})
        string(JSON entry GET "${db}" ${i})
        string(JSON file GET "${entry}" file)
        if(NOT file IN_LIST guest_files)
            list(APPEND host "${entry}")
        endif()
    endforeach()
endif()

# no path or flag here contains a ';', which would split the list
list(APPEND host ${entries})
list(JOIN host ",\n" merged)
set(merged "[\n${merged}\n]\n")
# rewriting an unchanged file would make clangd reload it
if(NOT merged STREQUAL db)
    file(WRITE ${DB} "${merged}")
endif()
