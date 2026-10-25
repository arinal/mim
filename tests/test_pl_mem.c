// SPDX-License-Identifier: MIT OR GPL-2.0-only

#define _GNU_SOURCE // sysconf

#include "mim_test.h"
#include "platform/pl.h"

#include <stdint.h>
#include <unistd.h>

int main(void) {
    const size_t page = (size_t)sysconf(_SC_PAGESIZE);
    const size_t size = 3 * page + 1; // not a whole number of pages

    void * ptr;
    CHECK(pl_mem_map(size, &ptr) == pl_result_ok);
    CHECK((uintptr_t)ptr % page == 0);

    unsigned char * const bytes = ptr;
    for (size_t i = 0; i < size; i++) {
        CHECK(bytes[i] == 0);
        bytes[i] = (unsigned char)i;
    }
    for (size_t i = 0; i < size; i++) {
        CHECK(bytes[i] == (unsigned char)i);
    }
    pl_mem_unmap(ptr, size);

    // no OS can map this much
    CHECK(pl_mem_map(SIZE_MAX, &ptr) == pl_result_out_of_memory);
    return 0;
}
