// SPDX-License-Identifier: MIT OR GPL-2.0-only

#include "mem.h"
#include "mim_test.h"

#include <stdint.h>

int main(void) {
    const mem_budget_def_t def = {"heap", 64 * 1024, mem_kind_heap};
    mem_budget_t heap;
    mem_t mem;
    CHECK(mem_init(&mem, &def, &heap, 1) == pl_result_ok);
    const size_t largest_at_start = mem_get_stats(&heap).largest_free;
    CHECK(largest_at_start > 60 * 1024 && largest_at_start < 64 * 1024);

    // round trip: aligned, filled with the "clean" pattern, writable
    unsigned char * const a = mem_alloc(&heap, 100, "a");
    CHECK((uintptr_t)a % 16 == 0);
    CHECK(a[0] == 0xCD && a[99] == 0xCD);
    a[0] = 1;
    a[99] = 1;
    mem_stats_t stats = mem_get_stats(&heap);
    CHECK(stats.used_blocks == 1 && stats.used > 100);
    CHECK(stats.free_blocks == 1); // what was left was split off
    mem_check(&heap);
    mem_free(&heap, a);
    mem_free(&heap, NULL);
    stats = mem_get_stats(&heap);
    CHECK(stats.used == 0 && stats.used_blocks == 0 && stats.free_blocks == 1);
    CHECK(stats.largest_free == largest_at_start);
    mem_check(&heap);

    // 0 bytes still gives a unique pointer
    void * const z1 = mem_alloc(&heap, 0, "z1");
    void * const z2 = mem_alloc(&heap, 0, "z2");
    CHECK(z1 != NULL && z2 != NULL && z1 != z2);
    mem_free(&heap, z1);
    mem_free(&heap, z2);
    mem_check(&heap);

    // first fit: a freed block of the right size is reused
    unsigned char * const x = mem_alloc(&heap, 100, "x");
    unsigned char * const y = mem_alloc(&heap, 100, "y");
    unsigned char * const w = mem_alloc(&heap, 100, "w");
    CHECK(x < y && y < w);
    mem_free(&heap, x);
    CHECK(mem_alloc(&heap, 100, "x again") == x);
    mem_check(&heap);

    // coalescing: free x, then w (merges with the free tail), then y (merges with both)
    mem_free(&heap, x);
    CHECK(mem_get_stats(&heap).free_blocks == 2);
    mem_free(&heap, w);
    CHECK(mem_get_stats(&heap).free_blocks == 2);
    mem_free(&heap, y);
    stats = mem_get_stats(&heap);
    CHECK(stats.free_blocks == 1 && stats.used == 0);
    CHECK(stats.largest_free == largest_at_start);
    mem_check(&heap);

    // largest_free is exact: it fits, one byte more doesn't
    CHECK(mem_try_alloc(&heap, largest_at_start + 1, "too big") == NULL);
    unsigned char * const all = mem_alloc(&heap, largest_at_start, "all");
    all[largest_at_start - 1] = 1;
    CHECK(mem_get_stats(&heap).largest_free == 0);
    CHECK(mem_try_alloc(&heap, 0, "nothing left") == NULL);
    CHECK(mem_get_stats(&heap).peak == mem_get_stats(&heap).size);
    mem_check(&heap);
    mem_free(&heap, all);

    // leaks: two blocks left behind are reported
    mem_alloc(&heap, 10, "leak_one");
    mem_alloc(&heap, 20, "leak_two");
    mem_check(&heap);
    CHECK(mem_shutdown(&mem) == 2);
    return 0;
}
