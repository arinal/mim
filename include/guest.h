//===-- guest.h - WASM guests: load, tick, unload -------------------------===//
//
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// A guest is an untrusted WASM module that exports
//   app_init()                                    called by guest_load
//   app_tick(i64 abs_time_ms, f32 dt_seconds)     called by guest_tick
//   app_shutdown()                                called by guest_unload
// and may import, from module "mim":
//   log(i32 level, i32 ptr, i32 len)              level 0 debug ... 3 error
//
// Rules:
// * A guest can fail, but never takes the host down. A bad module is
//   rejected at load; misbehaviour at runtime is a trap, after which the
//   guest is dead and only guest_unload does anything.
// * One guest at a time.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "mem.h"

#ifdef __cplusplus
extern "C" {
#endif

struct M3Environment;
struct M3Runtime;
struct M3Function;

/// One loaded guest; the caller owns the storage, the fields are private to guest.c
typedef struct guest_t {
    struct {
        mem_budget_t * runtime;
        mem_budget_t * linear;
        // the guest's linear memory, the only allocation in `linear`
        void * linear_block;
        struct M3Environment * env;
        struct M3Runtime * rt;
        struct M3Function * tick;
        struct M3Function * shutdown;
        // false once the guest is rejected or has trapped
        bool alive;
    } internal;
} guest_t;

/// Loads a WASM module, links the host imports, checks the exports and calls `app_init`
///
/// * `guest`: Filled in here
/// * `runtime`: An empty heap budget for wasm3 itself
/// * `linear`: An empty bump budget for the guest's linear memory
/// * `wasm`: The module; must stay valid until `guest_unload`
/// * `size`: Its size in bytes
///
/// Returns false, after logging why, if the module is rejected or `app_init` traps.
/// Either way, call `guest_unload` afterwards.
bool guest_load(guest_t * const guest, mem_budget_t * const runtime, mem_budget_t * const linear, const void * const wasm, const size_t size);

/// Calls `app_tick`
///
/// Returns false if the guest trapped now or earlier
bool guest_tick(guest_t * const guest, const uint64_t abs_time_ms, const float dt_seconds);

/// Calls `app_shutdown` if the guest is still alive, then frees everything it used
///
/// Both budgets are empty afterwards.
void guest_unload(guest_t * const guest);

#ifdef __cplusplus
}
#endif
