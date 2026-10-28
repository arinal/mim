//===-- guest.c - WASM guests on wasm3, with memory from budgets ----------===//
//
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// Everything wasm3 allocates goes to the guest's two budgets:
//   vm       wasm3 itself: parsed module, compiled code, value stack (a heap)
//   linear   the guest's linear memory: one block that grows in place (bump)
// wasm3 calls the hooks at the end of this file instead of malloc/free.
//
//===----------------------------------------------------------------------===//

#include "runtime/guest/guest.h"

#include "wasm3.h"

#include <stdarg.h>
#include <string.h>

enum {
    // wasm3's value stack: the WASM operand stack and call frames
    guest_stack_size = 512 * 1024,
    // a longer log message is cut
    guest_log_max = 200,
};

// ponytail: one guest at a time, because wasm3's allocation hooks take no context.
// Two guests at once need the runtime passed through the hooks (M5 patches wasm3 for that).
static guest_t *g_guest;

//===----------------------------------------------------------------------===//
// Imports
//===----------------------------------------------------------------------===//

// mim.log(i32 level, i32 ptr, i32 len)
static const void *mim_log(IM3Runtime runtime, IM3ImportContext ctx, uint64_t *sp, void *mem) {
    (void)ctx;
    // each argument is in its own 64-bit slot
    const uint32_t level = *(const uint32_t *)&sp[0];
    const uint32_t ptr = *(const uint32_t *)&sp[1];
    const uint32_t len = *(const uint32_t *)&sp[2];
    if (level > pl_log_severity_error) {
        return "mim.log: bad level";
    }
    // written so it can't overflow: ptr + len could wrap in 32 bits
    const size_t size = m3_GetMemorySize(runtime);
    if (len > size || ptr > size - len) {
        return "mim.log: buffer outside guest memory";
    }

    char buf[guest_log_max + 4];
    const size_t n = (len < guest_log_max) ? len : guest_log_max;
    memcpy(buf, (const unsigned char *)mem + ptr, n);
    // control bytes would let a guest forge host log lines or send terminal escapes
    for (size_t i = 0; i < n; i++) {
        const unsigned char c = (unsigned char)buf[i];
        if (c < 0x20 || c == 0x7F) {
            buf[i] = '?';
        }
    }
    size_t end = n;
    if (len > n) {
        memcpy(buf + n, "...", 3);
        end += 3;
    }
    pl_log((pl_log_severity_e)level, "app: %.*s", (int)end, buf);
    return m3Err_none;
}

// mim.present(i32 ptr, i32 width, i32 height)
static const void *mim_present(IM3Runtime runtime, IM3ImportContext ctx, uint64_t *sp, void *mem) {
    (void)ctx;
    const uint32_t ptr = *(const uint32_t *)&sp[0];
    const uint32_t width = *(const uint32_t *)&sp[1];
    const uint32_t height = *(const uint32_t *)&sp[2];
    if (width != guest_screen_width || height != guest_screen_height) {
        return "mim.present: frame must be 1280x720";
    }
    const size_t len = (size_t)guest_screen_width * guest_screen_height * 4;
    const size_t size = m3_GetMemorySize(runtime);
    if (len > size || ptr > size - len) {
        return "mim.present: frame outside guest memory";
    }
    // ptr needn't be aligned: memcpy doesn't care. The top byte of each pixel is ignored later.
    memcpy(g_guest->internal.framebuffer, (const unsigned char *)mem + ptr, len);
    return m3Err_none;
}

static const void *mim_add(IM3Runtime runtime, IM3ImportContext ctx, uint64_t *sp, void *mem) {
    (void)runtime;
    (void)ctx;
    (void)mem;
    const uint32_t a = (const uint32_t)sp[1];
    const uint32_t b = (const uint32_t)sp[2];
    sp[0] = a + b;
    return m3Err_none;
}

//===----------------------------------------------------------------------===//
// Load, tick, unload
//===----------------------------------------------------------------------===//

// Returns true if `f` takes exactly `args` (`count` of them) and returns `ret` (c_m3Type_none: nothing)
static bool has_signature(IM3Function const f, const M3ValueType ret, const M3ValueType *const args, const uint32_t count) {
    const uint32_t rets = ret == c_m3Type_none ? 0 : 1;
    if (m3_GetRetCount(f) != rets || (rets == 1 && m3_GetRetType(f, 0) != ret) || m3_GetArgCount(f) != count) {
        return false;
    }
    for (uint32_t i = 0; i < count; i++) {
        if (m3_GetArgType(f, i) != args[i]) {
            return false;
        }
    }
    return true;
}

// Finds the export `name` and checks its signature; logs and returns NULL if either fails
static IM3Function find_export(
    guest_t *const guest,
    const char *const name,
    const M3ValueType ret,
    const M3ValueType *const args,
    const uint32_t count
) {
    IM3Function f = NULL;
    const M3Result result = m3_FindFunction(&f, guest->internal.rt, name);
    if (result != m3Err_none) {
        pl_log(pl_log_severity_error, "guest: rejected: export '%s': %s", name, result);
        return NULL;
    }
    if (!has_signature(f, ret, args, count)) {
        pl_log(pl_log_severity_error, "guest: rejected: export '%s' has the wrong signature", name);
        return NULL;
    }
    return f;
}

// Calls into the guest; a trap kills it. Arguments as for m3_CallV.
static bool call(guest_t *const guest, IM3Function const f, ...) {
    va_list args;
    va_start(args, f);
    const M3Result result = m3_CallVL(f, args);
    va_end(args);
    if (result != m3Err_none) {
        pl_log(pl_log_severity_error, "guest: trapped in %s: %s", m3_GetFunctionName(f), result);
        guest->internal.alive = false;
    }
    return guest->internal.alive;
}

// Links mim.<name>; logs and returns false if the guest imports it with another signature.
// A guest that doesn't import it is fine.
static bool link_import(IM3Module const module, const char *const name, const char *const signature, M3RawCall const fn) {
    const M3Result result = m3_LinkRawFunction(module, "mim", name, signature, fn);
    if (result != m3Err_none && result != m3Err_functionLookupFailed) {
        pl_log(pl_log_severity_error, "guest: rejected: import 'mim.%s': %s", name, result);
        return false;
    }
    return true;
}

// Parses, loads and links the module; logs and returns false if it's rejected
static bool load_module(guest_t *const guest, const void *const wasm, const size_t size) {
    if (size > UINT32_MAX) {
        pl_log(pl_log_severity_error, "guest: rejected: %zu bytes is too big", size);
        return false;
    }
    guest->internal.env = m3_NewEnvironment();
    if (guest->internal.env != NULL) {
        guest->internal.rt = m3_NewRuntime(guest->internal.env, guest_stack_size, guest);
    }
    if (guest->internal.rt == NULL) {
        pl_log(pl_log_severity_error, "guest: rejected: budget '%s' is too small for wasm3", guest->internal.vm->internal.name);
        return false;
    }

    IM3Module module = NULL;
    M3Result result = m3_ParseModule(guest->internal.env, &module, wasm, (uint32_t)size);
    if (result == m3Err_none) {
        result = m3_LoadModule(guest->internal.rt, module);
        if (result != m3Err_none) {
            // only a module the runtime accepted belongs to it
            m3_FreeModule(module);
        }
    }
    if (result != m3Err_none) {
        pl_log(pl_log_severity_error, "guest: rejected: %s", result);
        return false;
    }

    return link_import(module, "log", "v(iii)", mim_log) && link_import(module, "present", "v(iii)", mim_present);
}

bool guest_load(
    guest_t *const guest,
    mem_budget_t *const vm,
    mem_budget_t *const linear,
    uint32_t *const framebuffer,
    const void *const wasm,
    const size_t size
) {
    assert(g_guest == NULL && "one guest at a time");
    assert(vm->internal.used_blocks == 0 && linear->internal.used_blocks == 0);
    memset(guest, 0, sizeof(*guest));
    guest->internal.vm = vm;
    guest->internal.linear = linear;
    guest->internal.framebuffer = framebuffer;
    g_guest = guest;

    if (!load_module(guest, wasm, size)) {
        return false;
    }
    static const M3ValueType tick_args[] = {c_m3Type_i64, c_m3Type_f32};
    IM3Function const init = find_export(guest, "app_init", c_m3Type_none, NULL, 0);
    guest->internal.tick = find_export(guest, "app_tick", c_m3Type_i32, tick_args, 2);
    guest->internal.shutdown = find_export(guest, "app_shutdown", c_m3Type_none, NULL, 0);
    if (init == NULL || guest->internal.tick == NULL || guest->internal.shutdown == NULL) {
        return false;
    }
    guest->internal.alive = true;
    return call(guest, init);
}

guest_status_e guest_tick(guest_t *const guest, const uint64_t abs_time_ms, const float dt_seconds) {
    if (!guest->internal.alive) {
        return guest_dead;
    }
    if (guest->internal.quit) {
        return guest_quit;
    }
    // m3_CallV takes an f32 as a double, the C rule for variadic arguments
    if (!call(guest, guest->internal.tick, abs_time_ms, (double)dt_seconds)) {
        return guest_dead;
    }
    // any value is valid: non-zero means "keep going", as in M5
    int32_t keep_going = 0;
    const M3Result result = m3_GetResultsV(guest->internal.tick, &keep_going);
    assert(result == m3Err_none && "find_export checked the result type");
    (void)result;
    guest->internal.quit = keep_going == 0;
    return guest->internal.quit ? guest_quit : guest_running;
}

void guest_unload(guest_t *const guest) {
    assert(g_guest == guest);
    if (guest->internal.alive) {
        call(guest, guest->internal.shutdown);
        guest->internal.alive = false;
    }
    // frees the modules and, through m3_LinearFree_Impl, the linear memory
    m3_FreeRuntime(guest->internal.rt);
    m3_FreeEnvironment(guest->internal.env);
    guest->internal.rt = NULL;
    guest->internal.env = NULL;
    assert(guest->internal.linear_block == NULL);
    g_guest = NULL;

    // anything left is a wasm3 leak; mem_shutdown would name it too, but only much later
    const mem_budget_t *const vm = guest->internal.vm;
    if (vm->internal.used_blocks != 0) {
        pl_log(pl_log_severity_warn, "guest: wasm3 leaked %zu blocks in budget '%s'", vm->internal.used_blocks, vm->internal.name);
    }
}

//===----------------------------------------------------------------------===//
// wasm3's allocation hooks (d_m3HostAllocator)
//
// Anything a guest can make bigger uses mem_try_*: a guest that asks for too
// much gets an error, never an abort of the host.
//===----------------------------------------------------------------------===//

void *m3_Malloc_Impl(size_t size) {
    assert(g_guest != NULL);
    void *const p = mem_try_alloc(g_guest->internal.vm, size, "wasm3");
    if (p != NULL) {
        memset(p, 0, size); // wasm3 expects calloc
    }
    return p;
}

void m3_Free_Impl(void *ptr) {
    assert(g_guest != NULL);
    mem_free(g_guest->internal.vm, ptr);
}

void *m3_Realloc_Impl(void *ptr, size_t new_size, size_t old_size) {
    if (new_size == old_size) {
        return ptr;
    }
    // the budgets have no realloc; wasm3 passes the old size, so copy
    void *const q = m3_Malloc_Impl(new_size);
    if (q != NULL && ptr != NULL) {
        memcpy(q, ptr, (old_size < new_size) ? old_size : new_size);
        m3_Free_Impl(ptr);
    }
    return q;
}

void *m3_LinearResize_Impl(void *ptr, size_t new_size, size_t old_size) {
    assert(g_guest != NULL);
    guest_t *const guest = g_guest;
    if (ptr == NULL) {
        // mem_try_grow only grows the last allocation, so there must never be a second one
        if (guest->internal.linear_block != NULL) {
            return NULL;
        }
        ptr = mem_try_alloc(guest->internal.linear, new_size, "linear memory");
        if (ptr == NULL) {
            return NULL;
        }
        guest->internal.linear_block = ptr;
        old_size = 0;
    } else {
        assert(ptr == guest->internal.linear_block);
        if (!mem_try_grow(guest->internal.linear, ptr, new_size)) {
            return NULL; // the guest's memory.grow returns -1
        }
    }
    // new WASM pages are zero; without this a guest would see what the budget held before
    if (new_size > old_size) {
        memset((unsigned char *)ptr + old_size, 0, new_size - old_size);
    }
    return ptr;
}

void m3_LinearFree_Impl(void *ptr) {
    assert(g_guest != NULL);
    if (ptr == NULL) {
        return;
    }
    assert(ptr == g_guest->internal.linear_block);
    mem_reset(g_guest->internal.linear);
    g_guest->internal.linear_block = NULL;
}
