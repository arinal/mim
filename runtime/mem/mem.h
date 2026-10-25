//===-- mem.h - Memory budgets: one region split into named budgets -------===//
//
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// Rules:
// * All engine memory comes from a budget. A full budget never takes memory
//   from another one.
// * Misuse (double free, writing past the end, a pointer from elsewhere) is
//   detected and aborts in every build, not only in debug builds.
// * Not thread-safe: each budget is used by one thread at a time.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "platform/pl.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// How a budget hands out memory
typedef enum mem_kind_e {
    /// Allocations only grow; freed all at once by `mem_reset`
    mem_kind_bump,
    /// Allocations are freed one by one (a first-fit free-list allocator)
    mem_kind_heap,
} mem_kind_e;

/// One budget as the caller asks for it
typedef struct mem_budget_def_t {
    /// Shown in reports; must outlive the budget
    const char * name;
    /// Rounded up to a multiple of 64
    size_t size;
    mem_kind_e kind;
} mem_budget_def_t;

struct mem_block_t;

/// One budget; the caller owns the storage, the fields are private to mem.c
typedef struct mem_budget_t {
    struct {
        const char * name;
        mem_kind_e kind;
        unsigned char * base;
        size_t size;
        // bump: bytes handed out so far; heap: bytes in used blocks, headers and guards included
        size_t used;
        size_t peak;
        size_t used_blocks;
        // heap only
        size_t free_blocks;
        struct mem_block_t * free_list;
        // heap only: end of the highest block ever handed out; free memory above it was never written
        unsigned char * touched;
        // bump only: the last allocation, the only one `mem_try_grow` can grow
        unsigned char * last;
    } internal;
} mem_budget_t;

/// The whole region
typedef struct mem_t {
    void * base;
    size_t size;
    mem_budget_t * budgets;
    size_t count;
} mem_t;

/// Usage of one budget
typedef struct mem_stats_t {
    /// The budget's size
    size_t size;
    /// Bytes in use; for a heap, headers and guards included
    size_t used;
    /// The highest `used` so far
    size_t peak;
    /// Live allocations
    size_t used_blocks;
    /// Heap only: blocks on the free list
    size_t free_blocks;
    /// The largest allocation that would succeed now
    size_t largest_free;
} mem_stats_t;

/// Maps one region big enough for all of `defs` and splits it into budgets
///
/// * `mem`: Filled in here
/// * `defs`: `count` budget definitions
/// * `budgets`: Caller-provided array of `count` budgets, filled in here, in the order of `defs`
///
/// Returns `pl_result_out_of_memory` if the region can't be mapped
pl_result_e mem_init(mem_t * const mem, const mem_budget_def_t * const defs, mem_budget_t * const budgets, const size_t count);

/// Logs peak usage and leaks for every budget, then unmaps the region and clears `mem`
///
/// A second call does nothing. Returns the number of leaked heap blocks (0 is good)
size_t mem_shutdown(mem_t * const mem);

/// Allocates `size` bytes aligned to 16; aborts with a report if they don't fit
///
/// * `budget`: The budget to allocate from
/// * `size`: Bytes to allocate; 0 returns a unique pointer
/// * `tag`: Shown in reports; a string literal naming the caller
void * mem_alloc(mem_budget_t * const budget, const size_t size, const char * const tag);

/// Same as `mem_alloc`, but returns NULL if the allocation doesn't fit
///
/// Use this for anything a guest or an input can make arbitrarily large.
void * mem_try_alloc(mem_budget_t * const budget, const size_t size, const char * const tag);

/// Frees `ptr`, which came from `mem_alloc` or `mem_try_alloc` on the same heap budget
///
/// NULL does nothing.
void mem_free(mem_budget_t * const budget, void * const ptr);

/// Frees every allocation of a bump budget at once
void mem_reset(mem_budget_t * const budget);

/// Grows the most recent allocation of a bump budget in place
///
/// * `ptr`: The most recent allocation
/// * `new_size`: Its new size; a size not above the current one does nothing
///
/// Returns false if the budget has no room; `ptr` stays valid either way
bool mem_try_grow(mem_budget_t * const budget, void * const ptr, const size_t new_size);

/// Returns the usage of `budget`
mem_stats_t mem_get_stats(const mem_budget_t * const budget);

/// Walks a heap budget and aborts with a report if anything is inconsistent
///
/// Checks block sizes, magic numbers, guard bytes, freed memory left untouched,
/// coalescing, the free list and the counters. O(budget size).
void mem_check(const mem_budget_t * const budget);

#ifdef __cplusplus
}
#endif
