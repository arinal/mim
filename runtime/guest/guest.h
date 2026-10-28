//===-- guest.h - WASM guests: load, tick, unload -------------------------===//
//
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// A guest is an untrusted WASM module that exports
//   app_init()                                    called by guest_load
//   app_tick(i64 abs_time_ms, f32 dt_seconds) -> i32
//                                                 called by guest_tick; 0 = quit
//   app_shutdown()                                called by guest_unload
// and may import the functions listed in sdk/mim.ffi, from module "mim".
//
// Rules:
// * A guest can fail, but never takes the host down. A bad module is
//   rejected at load; misbehaviour at runtime is a trap, after which the
//   guest is dead and only guest_unload does anything.
// * One guest at a time.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "runtime/guest/ffi.h"
#include "runtime/mem/mem.h"

#ifdef __cplusplus
extern "C" {
#endif

struct M3Environment;
struct M3Runtime;
struct M3Function;

/// One loaded guest; the caller owns the storage, the fields are private to guest.c
typedef struct guest_t {
    struct {
        mem_budget_t *vm;
        mem_budget_t *linear;
        // the guest's linear memory, the only allocation in `linear`
        void *linear_block;
        // guest_screen_width × guest_screen_height, filled by mim.present
        uint32_t *framebuffer;
        struct M3Environment *env;
        struct M3Runtime *rt;
        struct M3Function *tick;
        struct M3Function *shutdown;
        // false once the guest is rejected or has trapped
        bool alive;
        // true once app_tick has returned 0
        bool quit;
    } internal;
} guest_t;

/// Loads a WASM module, links the host imports, checks the exports and calls `app_init`
///
/// * `guest`: Filled in here
/// * `vm`: An empty heap budget for wasm3 itself
/// * `linear`: An empty bump budget for the guest's linear memory
/// * `framebuffer`: guest_screen_width × guest_screen_height pixels that mim.present copies
///   into; owned by the caller, valid until `guest_unload`
/// * `wasm`: The module; must stay valid until `guest_unload`
/// * `size`: Its size in bytes
///
/// Returns false, after logging why, if the module is rejected or `app_init` traps.
/// Either way, call `guest_unload` afterwards.
bool guest_load(
    guest_t *const guest,
    mem_budget_t *const vm,
    mem_budget_t *const linear,
    uint32_t *const framebuffer,
    const void *const wasm,
    const size_t size
);

/// What guest_tick found
typedef enum guest_status_e {
    /// app_tick returned non-zero: call it again next frame
    guest_running,
    /// app_tick returned 0: the guest is done; guest_unload still calls app_shutdown
    guest_quit,
    /// The guest trapped, now or before; guest_unload skips app_shutdown
    guest_dead,
} guest_status_e;

/// Calls `app_tick`, unless the guest has already quit or died
guest_status_e guest_tick(guest_t *const guest, const uint64_t abs_time_ms, const float dt_seconds);

/// Calls `app_shutdown` if the guest is still alive, then frees everything it used
///
/// Both budgets are empty afterwards.
void guest_unload(guest_t *const guest);

#ifdef __cplusplus
}
#endif
