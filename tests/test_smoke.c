#include "mim_test.h"

#include <string.h>

int main(void) {
    char *p = malloc(4);
    CHECK(p != NULL);
    memcpy(p, "mim", 4);
    CHECK(strcmp(p, "mim") == 0);
    free(p);
    return 0;
}
