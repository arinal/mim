// SPDX-License-Identifier: MIT OR GPL-2.0-only

#include "mim_test.h"

#include <string.h>

int main(void) {
    char * const p = malloc(4);
    CHECK(p != NULL);
    memcpy(p, "mim", 4);
    CHECK(strcmp(p, "mim") == 0);
    free(p);
    return 0;
}
