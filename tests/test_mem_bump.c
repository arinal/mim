// SPDX-License-Identifier: MIT OR GPL-2.0-only

#include "mim_test.h"
#include "runtime/mem/mem.h"

#include <stdint.h>

int main(void) {
    const mem_budget_def_t def = {"bump", 4096, mem_kind_bump};
    mem_budget_t budget;
    mem_t mem;
    CHECK(mem_init(&mem, &def, &budget, 1) == pl_result_ok);

    // aligned to 16 and packed
    unsigned char * const a = mem_alloc(&budget, 1, "a");
    unsigned char * const b = mem_alloc(&budget, 1, "b");
    CHECK(a == mem.base);
    CHECK((uintptr_t)b % 16 == 0 && b == a + 16);
    a[0] = 1;
    b[0] = 2;

    // 0 bytes still gives a unique pointer
    void * const z1 = mem_alloc(&budget, 0, "z");
    void * const z2 = mem_alloc(&budget, 0, "z");
    CHECK(z1 != NULL && z2 != NULL && z1 != z2);
    CHECK(mem_get_stats(&budget).used_blocks == 4);

    // exactly full, then one byte more fails
    mem_reset(&budget);
    CHECK(mem_get_stats(&budget).used == 0);
    unsigned char * const all = mem_alloc(&budget, 4096, "all");
    CHECK(all == mem.base);
    all[4095] = 1;
    CHECK(mem_get_stats(&budget).largest_free == 0);
    CHECK(mem_try_alloc(&budget, 1, "one more") == NULL);

    // reset gives the same memory again; peak survives
    mem_reset(&budget);
    CHECK(mem_alloc(&budget, 10, "again") == mem.base);
    CHECK(mem_get_stats(&budget).peak == 4096);
    CHECK(mem_try_alloc(&budget, 4097, "too big") == NULL);

    // growing the last allocation in place
    mem_reset(&budget);
    unsigned char * const p = mem_alloc(&budget, 100, "p");
    CHECK(mem_try_grow(&budget, p, 200));
    p[199] = 1;
    CHECK(mem_get_stats(&budget).used == 200);
    CHECK(mem_try_grow(&budget, p, 50)); // not bigger: nothing to do
    CHECK(mem_get_stats(&budget).used == 200);
    unsigned char * const q = mem_alloc(&budget, 10, "q");
    CHECK(q == p + 208);
    CHECK(!mem_try_grow(&budget, q, 4096 - 207));
    CHECK(mem_try_grow(&budget, q, 4096 - 208));
    q[4096 - 209] = 1;
    CHECK(mem_get_stats(&budget).largest_free == 0);

    // a 0-byte allocation grown to 1 byte is writable (its byte starts out poisoned under ASan)
    mem_reset(&budget);
    unsigned char * const z = mem_alloc(&budget, 0, "z");
    CHECK(mem_try_grow(&budget, z, 1));
    CHECK(z[0] == 0xCD);
    z[0] = 1;

    // fills: new and grown bytes are 0xCD; mem_reset leaves 0xDD behind
    mem_reset(&budget);
    unsigned char * const f = mem_alloc(&budget, 4, "f");
    CHECK(f[0] == 0xCD && f[3] == 0xCD);
    f[0] = 1;
    CHECK(mem_try_grow(&budget, f, 8));
    CHECK(f[0] == 1 && f[4] == 0xCD && f[7] == 0xCD);
    mem_reset(&budget);
#ifndef __SANITIZE_ADDRESS__ // under ASan, reading memory that was reset is stopped
    CHECK(f[0] == 0xDD && f[7] == 0xDD);
#endif

    // a second shutdown does nothing
    CHECK(mem_shutdown(&mem) == 0);

    CHECK(mem_shutdown(&mem) == 0);
    return 0;
}
