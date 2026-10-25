// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// Fuzzes both allocators. With MIM_FUZZ this is a libFuzzer target; otherwise main() feeds it
// inputs from a fixed-seed pseudo-random generator, so every build runs the same harness.
//
// Each 4 input bytes are one operation: op, slot, size (2 bytes). Bit 3 of op picks the bump budget.
// Every heap allocation is filled with its slot number and checked before it's freed, and the last
// bump allocation is checked before each bump operation, which catches allocations that overlap.

#include "mim_test.h"
#include "runtime/mem/mem.h"

#include <string.h>

enum {
    slot_count = 64,
    heap_size = 16 * 1024,
    bump_size = 4 * 1024,
    bump_fill = 0xAB,
};

static mem_t mem;
static mem_budget_t budgets[2];
static mem_budget_t * const heap = &budgets[0];
static mem_budget_t * const bump = &budgets[1];

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
    mem_free(heap, p);
    slots[slot] = NULL;
}

// The last bump allocation, the only one that can grow
static unsigned char * last;
static size_t last_size;

static bool bytes_are(const unsigned char * const p, const unsigned char byte, const size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (p[i] != byte) {
            return false;
        }
    }
    return true;
}

static void bump_op(const unsigned op, const size_t n) {
    CHECK(last == NULL || bytes_are(last, bump_fill, last_size));
    size_t clean_from; // where the bytes that are new to the caller start
    if (op < 4) {
        last = mem_try_alloc(bump, n, "fuzz");
        if (last == NULL) {
            mem_reset(bump);
            return;
        }
        clean_from = 0;
    } else if (last != NULL && mem_try_grow(bump, last, n) && n > last_size) {
        clean_from = last_size;
    } else {
        return;
    }
    // new bytes are clean; then the whole allocation is written, to its last byte
    CHECK(bytes_are(last + clean_from, 0xCD, n - clean_from));
    memset(last, bump_fill, n);
    last_size = n;
}

int LLVMFuzzerTestOneInput(const unsigned char * const data, const size_t size);

int LLVMFuzzerTestOneInput(const unsigned char * const data, const size_t size) {
    // One region for the whole run: each input must leave the heap as it found it (checked at the end)
    if (mem.base == NULL) {
        static const mem_budget_def_t defs[] = {
            {"fuzz heap", heap_size, mem_kind_heap},
            {"fuzz bump", bump_size, mem_kind_bump},
        };
        CHECK(mem_init(&mem, defs, budgets, 2) == pl_result_ok);
    }

    for (size_t i = 0; i + 4 <= size; i += 4) {
        const unsigned op = data[i] % 8;
        const size_t slot = data[i + 1] % slot_count;
        // the top bits of `op` shift the size down, so small and large sizes are both common
        const size_t n = ((size_t)data[i + 2] | (size_t)data[i + 3] << 8) >> (data[i] >> 4);
        if (data[i] & 8) {
            bump_op(op, n);
        } else if (op < 4) {
            release(slot);
            slots[slot] = mem_try_alloc(heap, n, "fuzz");
            if (slots[slot] != NULL) {
                memset(slots[slot], (int)slot, n);
                sizes[slot] = n;
            }
        } else if (op < 7) {
            release(slot);
        } else {
            mem_check(heap);
        }
    }

    for (size_t slot = 0; slot < slot_count; slot++) {
        release(slot);
    }
    mem_check(heap);
    // everything freed and merged back into one block
    const mem_stats_t stats = mem_get_stats(heap);
    CHECK(stats.used == 0 && stats.used_blocks == 0 && stats.free_blocks == 1);
    mem_reset(bump);
    last = NULL;
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
