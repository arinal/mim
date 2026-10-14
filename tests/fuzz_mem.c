// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// Fuzzes the heap allocator. With MIM_FUZZ this is a libFuzzer target; otherwise main() feeds it
// inputs from a fixed-seed pseudo-random generator, so every build runs the same harness.
//
// Each 4 input bytes are one operation: op, slot, size (2 bytes). Every allocation is filled with
// its slot number and checked before it's freed, which catches two allocations that overlap.

#include "mem.h"
#include "mim_test.h"

#include <string.h>

enum {
    slot_count = 64,
    heap_size = 16 * 1024,
};

static mem_t mem;
static mem_budget_t heap;

static unsigned char * slots[slot_count];
static size_t sizes[slot_count];

static void release(const size_t slot) {
    unsigned char * const p = slots[slot];
    if (p == NULL) {
        return;
    }
    for (size_t i = 0; i < sizes[slot]; i++) {
        CHECK(p[i] == (unsigned char)slot);
    }
    mem_free(&heap, p);
    slots[slot] = NULL;
}

int LLVMFuzzerTestOneInput(const unsigned char * const data, const size_t size);

int LLVMFuzzerTestOneInput(const unsigned char * const data, const size_t size) {
    // One heap for the whole run: each input must leave it as it found it (checked at the end)
    if (mem.base == NULL) {
        static const mem_budget_def_t def = {"fuzz", heap_size, mem_kind_heap};
        CHECK(mem_init(&mem, &def, &heap, 1) == pl_result_ok);
    }

    for (size_t i = 0; i + 4 <= size; i += 4) {
        const unsigned op = data[i] % 8;
        const size_t slot = data[i + 1] % slot_count;
        // the top bits of `op` shift the size down, so small and large sizes are both common
        const size_t n = ((size_t)data[i + 2] | (size_t)data[i + 3] << 8) >> (data[i] >> 4);
        if (op < 4) {
            release(slot);
            slots[slot] = mem_try_alloc(&heap, n, "fuzz");
            if (slots[slot] != NULL) {
                memset(slots[slot], (int)slot, n);
                sizes[slot] = n;
            }
        } else if (op < 7) {
            release(slot);
        } else {
            mem_check(&heap);
        }
    }

    for (size_t slot = 0; slot < slot_count; slot++) {
        release(slot);
    }
    mem_check(&heap);
    // everything freed and merged back into one block
    const mem_stats_t stats = mem_get_stats(&heap);
    CHECK(stats.used == 0 && stats.used_blocks == 0 && stats.free_blocks == 1);
    return 0;
}

#ifndef MIM_FUZZ
int main(void) {
    unsigned char input[256];
    unsigned x = 12345; // xorshift32
    for (int run = 0; run < 2000; run++) {
        for (size_t i = 0; i < sizeof(input); i++) {
            x ^= x << 13;
            x ^= x >> 17;
            x ^= x << 5;
            input[i] = (unsigned char)x;
        }
        LLVMFuzzerTestOneInput(input, sizeof(input));
    }
    CHECK(mem_shutdown(&mem) == 0);
    return 0;
}
#endif
