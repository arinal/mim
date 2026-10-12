#ifndef MIM_TEST_H
#define MIM_TEST_H

#include <stdio.h>
#include <stdlib.h>

/* One test executable per file; the first failing CHECK fails the whole executable. */
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);               \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

#endif
