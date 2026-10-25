// SPDX-License-Identifier: MIT OR GPL-2.0-only

#include "mim_test.h"
#include "runtime/mem/mem.h"

#include <stdint.h>

int main(void) {
    const mem_budget_def_t defs[] = {
        {"first", 100, mem_kind_heap},
        {"second", 64, mem_kind_bump},
        {"third", 1000, mem_kind_heap},
    };
    const size_t rounded[] = {128, 64, 1024};
    mem_budget_t budgets[3];
    mem_t mem;
    CHECK(mem_init(&mem, defs, budgets, 3) == pl_result_ok);
    CHECK(mem.size == 128 + 64 + 1024);
    CHECK(mem.budgets == budgets && mem.count == 3);

    // in order, each on a 64-byte boundary, right after the previous one
    unsigned char * expected = mem.base;
    for (size_t i = 0; i < 3; i++) {
        const mem_stats_t stats = mem_get_stats(&budgets[i]);
        CHECK(budgets[i].internal.base == expected);
        CHECK((uintptr_t)budgets[i].internal.base % 64 == 0);
        CHECK(stats.size == rounded[i]);
        CHECK(stats.used == 0 && stats.peak == 0 && stats.used_blocks == 0);
        expected += rounded[i];
    }
    CHECK(expected == (unsigned char *)mem.base + mem.size);

    CHECK(mem_get_stats(&budgets[0]).free_blocks == 1);
    CHECK(mem_get_stats(&budgets[1]).largest_free == 64);
    mem_check(&budgets[0]);
    mem_check(&budgets[2]);

    CHECK(mem_shutdown(&mem) == 0);
    return 0;
}
