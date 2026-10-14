//===-- mem.c - Memory budgets: a bump and a first-fit heap allocator -----===//
//
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// A heap budget is a sequence of blocks that covers it exactly:
//
//   | header | data (requested) | guard (16+) | header | data | guard | ...
//            ^ the pointer the caller gets
//
// Free blocks are linked in address order, so a freed block merges with
// both neighbours in the same walk that finds its place in the list.
//
//===----------------------------------------------------------------------===//

#include "mem.h"

#include <stdlib.h>
#include <string.h>

// Poisoned bytes make ASan stop at the access. Without ASan these do nothing.
#ifdef __SANITIZE_ADDRESS__
#include <sanitizer/asan_interface.h>
#else
#define ASAN_POISON_MEMORY_REGION(_addr, _size) ((void)(_addr), (void)(_size))
#define ASAN_UNPOISON_MEMORY_REGION(_addr, _size) ((void)(_addr), (void)(_size))
#endif

// Logs the reason and aborts; for full budgets and misuse, in every build
#define MEM_FAIL(...)                               \
    do {                                            \
        pl_log(pl_log_severity_error, __VA_ARGS__); \
        abort();                                    \
    } while (0)

enum {
    // Every allocation is aligned to this: alignof(max_align_t) on x86-64 and aarch64
    mem_align = 16,
    // Budgets start on a cache line, so two budgets never share one
    mem_budget_align = 64,
    // At least this many guard bytes follow each heap allocation
    mem_guard_size = 16,
};

// Fill patterns, the same values as MSVC's debug heap
enum {
    mem_fill_clean = 0xCD, // a new allocation
    mem_fill_dead = 0xDD, // freed memory
    mem_fill_guard = 0xFD, // after each allocation
};

static const uint32_t mem_magic_used = 0xA110CA7Eu;
static const uint32_t mem_magic_free = 0xF4EEB10Cu;

typedef struct mem_block_t {
    // The whole block: header, data and guard; a multiple of mem_align
    size_t size;
    // From mem_alloc; a free block keeps its last owner's, for double-free reports
    const char * tag;
    // The next free block by address (free blocks only)
    struct mem_block_t * next_free;
    // The bytes the caller asked for (used blocks only)
    uint32_t requested;
    // Last, right before the data: writing just before the pointer damages it first
    uint32_t magic;
} mem_block_t;

// C99 has no static_assert: an array with negative size is a compile error
typedef char mem_static_assert_header_keeps_data_aligned[(sizeof(mem_block_t) % mem_align == 0) ? 1 : -1];

// The smallest block: a header and the guard of a 0-byte allocation
#define MEM_MIN_BLOCK (sizeof(mem_block_t) + mem_guard_size)

static size_t round_up(const size_t x, const size_t align) {
    return (x + align - 1) & ~(align - 1);
}

static unsigned char * block_data(mem_block_t * const block) {
    return (unsigned char *)(block + 1);
}

static size_t block_data_size(const mem_block_t * const block) {
    return block->size - sizeof(mem_block_t);
}

// Returns true if all `n` bytes at `p` equal `byte`. They are poisoned before and after.
static bool bytes_are(const unsigned char * const p, const unsigned char byte, const size_t n) {
    ASAN_UNPOISON_MEMORY_REGION(p, n);
    size_t i = 0;
    while (i < n && p[i] == byte) {
        i++;
    }
    ASAN_POISON_MEMORY_REGION(p, n);
    return i == n;
}

//===----------------------------------------------------------------------===//
// Region
//===----------------------------------------------------------------------===//

static void heap_init(mem_budget_t * const budget) {
    // one free block covering everything; its data stays unwritten until something is allocated there
    mem_block_t * const block = (mem_block_t *)budget->internal.base;
    ASAN_UNPOISON_MEMORY_REGION(block, sizeof(*block));
    block->size = budget->internal.size;
    block->tag = "(never allocated)";
    block->next_free = NULL;
    block->requested = 0;
    block->magic = mem_magic_free;
    budget->internal.free_list = block;
    budget->internal.free_blocks = 1;
    budget->internal.touched = budget->internal.base;
}

pl_result_e mem_init(mem_t * const mem, const mem_budget_def_t * const defs, mem_budget_t * const budgets, const size_t count) {
    assert(count > 0);
    size_t total = 0;
    for (size_t i = 0; i < count; i++) {
        assert(defs[i].name != NULL && defs[i].size > 0);
        assert(defs[i].kind == mem_kind_bump || defs[i].kind == mem_kind_heap);
        total += round_up(defs[i].size, mem_budget_align);
    }

    void * base;
    const pl_result_e result = pl_mem_map(total, &base);
    if (result != pl_result_ok) {
        return result;
    }
    mem->base = base;
    mem->size = total;
    mem->budgets = budgets;
    mem->count = count;

    unsigned char * next = base;
    for (size_t i = 0; i < count; i++) {
        mem_budget_t * const budget = &budgets[i];
        memset(budget, 0, sizeof(*budget));
        budget->internal.name = defs[i].name;
        budget->internal.kind = defs[i].kind;
        budget->internal.base = next;
        budget->internal.size = round_up(defs[i].size, mem_budget_align);
        ASAN_POISON_MEMORY_REGION(next, budget->internal.size);
        if (defs[i].kind == mem_kind_heap) {
            heap_init(budget);
        }
        next += budget->internal.size;
    }
    return pl_result_ok;
}

// Returns the block after `block`, or NULL at the end of the budget; aborts if `block`'s size is impossible
static mem_block_t * next_block(const mem_budget_t * const budget, mem_block_t * const block) {
    const size_t offset = (size_t)((unsigned char *)block - budget->internal.base);
    const size_t left = budget->internal.size - offset;
    if (block->size < MEM_MIN_BLOCK || block->size % mem_align != 0 || block->size > left) {
        MEM_FAIL("mem: budget '%s': damaged block header at %p (size %zu)", budget->internal.name, (void *)block, block->size);
    }
    return (block->size == left) ? NULL : (mem_block_t *)((unsigned char *)block + block->size);
}

size_t mem_shutdown(mem_t * const mem) {
    if (mem->base == NULL) {
        return 0; // already shut down
    }
    size_t leaks = 0;
    for (size_t i = 0; i < mem->count; i++) {
        const mem_budget_t * const budget = &mem->budgets[i];
        const char * const name = budget->internal.name;
        const size_t size = budget->internal.size;
        pl_log(pl_log_severity_info, "mem: '%s' peak %zu of %zu (%zu%%)", name, budget->internal.peak, size, budget->internal.peak * 100 / size);
        if (budget->internal.kind != mem_kind_heap) {
            continue;
        }
        for (mem_block_t * block = (mem_block_t *)budget->internal.base; block != NULL; block = next_block(budget, block)) {
            if (block->magic == mem_magic_used) {
                pl_log(pl_log_severity_warn, "mem: '%s' leak: %zu bytes, tag '%s'", name, (size_t)block->requested, block->tag);
                leaks++;
            }
        }
    }
    // the OS may hand these addresses out again, so nothing in them may stay poisoned
    ASAN_UNPOISON_MEMORY_REGION(mem->base, mem->size);
    pl_mem_unmap(mem->base, mem->size);
    memset(mem, 0, sizeof(*mem));
    return leaks;
}

//===----------------------------------------------------------------------===//
// Bump
//===----------------------------------------------------------------------===//

static void * bump_alloc(mem_budget_t * const budget, const size_t size) {
    const size_t start = round_up(budget->internal.used, mem_align);
    // 0 bytes still take one, so every pointer is unique
    const size_t n = (size > 0) ? size : 1;
    if (n > budget->internal.size - start) {
        return NULL;
    }
    unsigned char * const ptr = budget->internal.base + start;
    budget->internal.used = start + n;
    budget->internal.used_blocks++;
    budget->internal.last = ptr;
    ASAN_UNPOISON_MEMORY_REGION(ptr, n);
    memset(ptr, mem_fill_clean, n);
    // the byte of a 0-byte allocation stays off limits
    ASAN_POISON_MEMORY_REGION(ptr + size, n - size);
    return ptr;
}

void mem_reset(mem_budget_t * const budget) {
    if (budget->internal.kind != mem_kind_bump) {
        MEM_FAIL("mem: budget '%s' is a heap budget: free its allocations with mem_free", budget->internal.name);
    }
    // padding between allocations is poisoned too, so all of it is unpoisoned for the fill
    ASAN_UNPOISON_MEMORY_REGION(budget->internal.base, budget->internal.used);
    memset(budget->internal.base, mem_fill_dead, budget->internal.used);
    ASAN_POISON_MEMORY_REGION(budget->internal.base, budget->internal.size);
    budget->internal.used = 0;
    budget->internal.used_blocks = 0;
    budget->internal.last = NULL;
}

bool mem_try_grow(mem_budget_t * const budget, void * const ptr, const size_t new_size) {
    if (budget->internal.kind != mem_kind_bump) {
        MEM_FAIL("mem: budget '%s' is a heap budget: mem_try_grow needs a bump budget", budget->internal.name);
    }
    if (ptr == NULL || ptr != budget->internal.last) {
        MEM_FAIL("mem: budget '%s': mem_try_grow on %p, which is not the last allocation", budget->internal.name, ptr);
    }
    const size_t start = (size_t)(budget->internal.last - budget->internal.base);
    if (new_size > budget->internal.size - start) {
        return false;
    }
    // even when not growing: a 0-byte allocation holds 1 poisoned byte, which growing to 1 hands out
    ASAN_UNPOISON_MEMORY_REGION(ptr, new_size);
    const size_t old_size = budget->internal.used - start;
    if (new_size <= old_size) {
        return true;
    }
    memset((unsigned char *)ptr + old_size, mem_fill_clean, new_size - old_size);
    budget->internal.used = start + new_size;
    if (budget->internal.used > budget->internal.peak) {
        budget->internal.peak = budget->internal.used;
    }
    return true;
}

//===----------------------------------------------------------------------===//
// Heap
//===----------------------------------------------------------------------===//

// Aborts if `block`, reached through the free list, isn't a free block of this budget
static void heap_check_free(const mem_budget_t * const budget, const mem_block_t * const block) {
    const uintptr_t p = (uintptr_t)block;
    const uintptr_t base = (uintptr_t)budget->internal.base;
    if (p < base || p > base + budget->internal.size - MEM_MIN_BLOCK || (p - base) % mem_align != 0 || block->magic != mem_magic_free) {
        MEM_FAIL("mem: budget '%s': free list damaged at %p (a write before a freed block?)", budget->internal.name, (const void *)block);
    }
}

static void * heap_alloc(mem_budget_t * const budget, const size_t size, const char * const tag) {
    if (size > UINT32_MAX) {
        return NULL;
    }
    const size_t need = round_up(sizeof(mem_block_t) + size + mem_guard_size, mem_align);

    // first fit
    mem_block_t * prev = NULL;
    mem_block_t * block = budget->internal.free_list;
    while (block != NULL) {
        heap_check_free(budget, block);
        if (block->size >= need) {
            break;
        }
        prev = block;
        block = block->next_free;
    }
    if (block == NULL) {
        return NULL;
    }

    // split off the rest if it can hold a block; otherwise it becomes part of this block's guard
    mem_block_t * next_free = block->next_free;
    if (block->size - need >= MEM_MIN_BLOCK) {
        mem_block_t * const rest = (mem_block_t *)((unsigned char *)block + need);
        ASAN_UNPOISON_MEMORY_REGION(rest, sizeof(*rest));
        rest->size = block->size - need;
        rest->tag = block->tag;
        rest->next_free = next_free;
        rest->requested = 0;
        rest->magic = mem_magic_free;
        block->size = need;
        next_free = rest;
    } else {
        budget->internal.free_blocks--;
    }
    if (prev != NULL) {
        prev->next_free = next_free;
    } else {
        budget->internal.free_list = next_free;
    }

    block->tag = tag;
    block->next_free = NULL;
    block->requested = (uint32_t)size;
    block->magic = mem_magic_used;

    unsigned char * const data = block_data(block);
    const size_t data_size = block_data_size(block);
    ASAN_UNPOISON_MEMORY_REGION(data, data_size);
    memset(data, mem_fill_clean, size);
    memset(data + size, mem_fill_guard, data_size - size);
    ASAN_POISON_MEMORY_REGION(data + size, data_size - size);

    budget->internal.used += block->size;
    budget->internal.used_blocks++;
    if (data + data_size > budget->internal.touched) {
        budget->internal.touched = data + data_size;
    }
    return data;
}

// Turns a valid pointer back into its block; aborts if it isn't a live block of this budget
static mem_block_t * heap_block_of(const mem_budget_t * const budget, void * const ptr) {
    const uintptr_t p = (uintptr_t)ptr;
    const uintptr_t base = (uintptr_t)budget->internal.base;
    if (p < base + sizeof(mem_block_t) || p >= base + budget->internal.size || (p - base) % mem_align != 0) {
        MEM_FAIL("mem: budget '%s': %p is not from this budget", budget->internal.name, ptr);
    }
    mem_block_t * const block = (mem_block_t *)ptr - 1;
    // a header merged into the free block before it is poisoned; every path below that isn't a live block aborts
    ASAN_UNPOISON_MEMORY_REGION(block, sizeof(*block));
    if (block->magic == mem_magic_free) {
        MEM_FAIL("mem: budget '%s': double free of %p (last tag '%s')", budget->internal.name, ptr, block->tag);
    }
    if (block->magic != mem_magic_used) {
        MEM_FAIL("mem: budget '%s': %p is not a live block (a double free after a merge, a damaged header, or never allocated)", budget->internal.name, ptr);
    }
    return block;
}

static void heap_check_guard(const mem_budget_t * const budget, mem_block_t * const block) {
    const size_t requested = block->requested;
    if (!bytes_are(block_data(block) + requested, mem_fill_guard, block_data_size(block) - requested)) {
        MEM_FAIL("mem: budget '%s': guard bytes after a %zu-byte block damaged (tag '%s')", budget->internal.name, requested, block->tag);
    }
}

// A header absorbed by the free block before it becomes free data
static void heap_bury(mem_block_t * const block) {
    memset(block, mem_fill_dead, sizeof(*block));
    ASAN_POISON_MEMORY_REGION(block, sizeof(*block));
}

void mem_free(mem_budget_t * const budget, void * const ptr) {
    if (ptr == NULL) {
        return;
    }
    if (budget->internal.kind != mem_kind_heap) {
        MEM_FAIL("mem: budget '%s' is a bump budget: use mem_reset, not mem_free", budget->internal.name);
    }
    mem_block_t * const block = heap_block_of(budget, ptr);
    heap_check_guard(budget, block);

    budget->internal.used -= block->size;
    budget->internal.used_blocks--;
    unsigned char * const data = block_data(block);
    ASAN_UNPOISON_MEMORY_REGION(data, block_data_size(block));
    memset(data, mem_fill_dead, block_data_size(block));
    ASAN_POISON_MEMORY_REGION(data, block_data_size(block));
    block->requested = 0;
    block->magic = mem_magic_free;

    // find the free neighbours: the list is in address order
    mem_block_t * prev = NULL;
    mem_block_t * next = budget->internal.free_list;
    while (next != NULL) {
        heap_check_free(budget, next);
        if (next > block) {
            break;
        }
        prev = next;
        next = next->next_free;
    }
    budget->internal.free_blocks++;

    if (next != NULL && (unsigned char *)block + block->size == (unsigned char *)next) {
        block->size += next->size;
        block->next_free = next->next_free;
        heap_bury(next);
        budget->internal.free_blocks--;
    } else {
        block->next_free = next;
    }

    if (prev != NULL && (unsigned char *)prev + prev->size == (unsigned char *)block) {
        prev->size += block->size;
        prev->next_free = block->next_free;
        heap_bury(block);
        budget->internal.free_blocks--;
    } else if (prev != NULL) {
        prev->next_free = block;
    } else {
        budget->internal.free_list = block;
    }
}

void mem_check(const mem_budget_t * const budget) {
    if (budget->internal.kind != mem_kind_heap) {
        return;
    }
    const char * const name = budget->internal.name;
    size_t used = 0;
    size_t used_blocks = 0;
    size_t free_blocks = 0;
    const mem_block_t * expected_free = budget->internal.free_list;
    bool prev_free = false;

    mem_block_t * block = (mem_block_t *)budget->internal.base;
    while (block != NULL) {
        // validates block->size before anything below relies on it
        mem_block_t * const next = next_block(budget, block);
        if (block->magic == mem_magic_used) {
            if (block->requested > block->size - MEM_MIN_BLOCK) {
                MEM_FAIL("mem: budget '%s': damaged block header at %p (requested %zu)", name, (void *)block, (size_t)block->requested);
            }
            heap_check_guard(budget, block);
            used += block->size;
            used_blocks++;
            prev_free = false;
        } else if (block->magic == mem_magic_free) {
            if (prev_free) {
                MEM_FAIL("mem: budget '%s': free block at %p was not merged with the one before it", name, (void *)block);
            }
            if (block != expected_free) {
                MEM_FAIL("mem: budget '%s': free list doesn't match the heap at %p", name, (void *)block);
            }
            expected_free = block->next_free;
            // memory above `touched` was never written, so it isn't 0xDD
            unsigned char * const data = block_data(block);
            unsigned char * const end = data + block_data_size(block);
            unsigned char * const written_end = (end < budget->internal.touched) ? end : budget->internal.touched;
            if (data < written_end && !bytes_are(data, mem_fill_dead, (size_t)(written_end - data))) {
                MEM_FAIL("mem: budget '%s': freed memory at %p was written after free (last tag '%s')", name, (void *)data, block->tag);
            }
            free_blocks++;
            prev_free = true;
        } else {
            MEM_FAIL("mem: budget '%s': damaged block header at %p (magic 0x%08x)", name, (void *)block, (unsigned)block->magic);
        }
        block = next;
    }

    if (expected_free != NULL) {
        MEM_FAIL("mem: budget '%s': free list points outside the heap (%p)", name, (const void *)expected_free);
    }
    if (used != budget->internal.used || used_blocks != budget->internal.used_blocks || free_blocks != budget->internal.free_blocks) {
        MEM_FAIL("mem: budget '%s': counters don't match the heap (used %zu/%zu, blocks %zu/%zu, free blocks %zu/%zu)", name, used, budget->internal.used, used_blocks, budget->internal.used_blocks, free_blocks, budget->internal.free_blocks);
    }
}

//===----------------------------------------------------------------------===//
// Both kinds
//===----------------------------------------------------------------------===//

void * mem_try_alloc(mem_budget_t * const budget, const size_t size, const char * const tag) {
    assert(tag != NULL);
    // also keeps the size arithmetic in both allocators from overflowing
    if (size > budget->internal.size) {
        return NULL;
    }
    void * const ptr = (budget->internal.kind == mem_kind_bump) ? bump_alloc(budget, size) : heap_alloc(budget, size, tag);
    if (budget->internal.used > budget->internal.peak) {
        budget->internal.peak = budget->internal.used;
    }
    return ptr;
}

void * mem_alloc(mem_budget_t * const budget, const size_t size, const char * const tag) {
    void * const ptr = mem_try_alloc(budget, size, tag);
    if (ptr == NULL) {
        const mem_stats_t stats = mem_get_stats(budget);
        pl_log(pl_log_severity_error, "mem: budget '%s' is full: wanted %zu bytes (tag '%s')", budget->internal.name, size, tag);
        MEM_FAIL("mem:   used %zu of %zu (peak %zu), %zu blocks, largest free block %zu", stats.used, stats.size, stats.peak, stats.used_blocks, stats.largest_free);
    }
    return ptr;
}

mem_stats_t mem_get_stats(const mem_budget_t * const budget) {
    mem_stats_t stats = {
        .size = budget->internal.size,
        .used = budget->internal.used,
        .peak = budget->internal.peak,
        .used_blocks = budget->internal.used_blocks,
        .free_blocks = budget->internal.free_blocks,
        .largest_free = 0,
    };
    if (budget->internal.kind == mem_kind_bump) {
        stats.largest_free = budget->internal.size - round_up(budget->internal.used, mem_align);
    } else {
        for (const mem_block_t * block = budget->internal.free_list; block != NULL; block = block->next_free) {
            heap_check_free(budget, block);
            if (block->size - MEM_MIN_BLOCK > stats.largest_free) {
                stats.largest_free = block->size - MEM_MIN_BLOCK;
            }
        }
    }
    return stats;
}
