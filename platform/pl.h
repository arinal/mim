//===-- pl.h - Platform layer: log, time, files, memory, threads, atomics -===//
//
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// Rules:
// * Only platform/<platform>/ may include OS headers. Everything else
//   includes this file.
// * pl_* never allocates. The caller provides buffers and storage.
// * Environment failures return pl_result_e. Caller bugs are caught by assert
//   in debug builds.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifndef __GNUC__
#error "pl.h needs GCC or clang (__atomic builtins, format attribute)"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/// The result of a platform call that can fail because of the environment
typedef enum pl_result_e {
    /// The call succeeded
    pl_result_ok,
    /// The file does not exist
    pl_result_not_found,
    /// The file is larger than the buffer
    pl_result_too_big,
    /// The OS refused to provide memory
    pl_result_out_of_memory,
    /// Any other failure
    pl_result_io_error,
    /// The environment lacks it, e.g. a window with no display to show it on
    pl_result_not_supported,
} pl_result_e;

//===----------------------------------------------------------------------===//
// Log
//===----------------------------------------------------------------------===//

/// The severity of a log message
typedef enum pl_log_severity_e {
    pl_log_severity_debug,
    pl_log_severity_info,
    pl_log_severity_warn,
    pl_log_severity_error,
} pl_log_severity_e;

/// Writes one line: `<D|I|W|E> <seconds>.<ms> <message>\n`
///
/// The line is written in a single call, so lines from different threads never mix.
/// Messages over ~500 bytes are cut off and end with `...`.
///
/// * `severity`: The severity of the message
/// * `fmt`: A printf format string, checked by the compiler
void pl_log(const pl_log_severity_e severity, const char *const fmt, ...) __attribute__((format(printf, 2, 3)));

//===----------------------------------------------------------------------===//
// Time
//===----------------------------------------------------------------------===//

/// Returns the monotonic clock in nanoseconds; the zero point is arbitrary
uint64_t pl_time_now_ns(void);

/// Sleeps for at least `ms` milliseconds
void pl_sleep_ms(const uint32_t ms);

/// Sleeps until pl_time_now_ns() >= deadline_ns; returns at once if that has passed
void pl_sleep_until_ns(const uint64_t deadline_ns);

//===----------------------------------------------------------------------===//
// Events
//===----------------------------------------------------------------------===//

/// What a pl_event_t says happened
typedef enum pl_event_type_e {
    /// The frame's timestamp: always the last event pl_tick returns
    pl_event_time,
    /// The user asked MIM to stop: Ctrl-C, SIGTERM, or the window's close button
    pl_event_quit,
    /// A key went down or up; the OS's key repeat is dropped
    pl_event_key,
} pl_event_type_e;

/// The keys a TV remote has
typedef enum pl_key_e {
    pl_key_left,
    pl_key_right,
    pl_key_up,
    pl_key_down,
    /// Enter
    pl_key_ok,
    /// Escape or Backspace
    pl_key_back,
} pl_key_e;

typedef struct pl_event_t {
    pl_event_type_e type;
    /// pl_time_now_ns() when pl_tick picked the event up
    uint64_t time_ns;
    /// pl_event_key only: which key, and whether it went down
    pl_key_e key;
    bool down;
} pl_event_t;

/// Collects what happened since the last call, without waiting
///
/// The first call installs the Ctrl-C and SIGTERM handlers. They run once: a second Ctrl-C
/// kills the process, which stops a guest stuck inside a frame.
///
/// * `events`: Buffer owned by the caller
/// * `cap`: Its size, at least 2
///
/// Returns how many events it wrote: the keys in the order they happened (any beyond `cap - 2`
/// are dropped), then at most one pl_event_quit, then pl_event_time last
size_t pl_tick(pl_event_t *const events, const size_t cap);

//===----------------------------------------------------------------------===//
// Display
//
// One window at a time, on the main thread. While it's open, pl_tick also
// collects its events. The window system allocates its own memory, outside
// MIM's budgets.
//===----------------------------------------------------------------------===//

/// Opaque storage for a window, owned by the caller
typedef struct pl_display_t {
    union {
        uint64_t align;
        unsigned char bytes[32];
    } u;
} pl_display_t;

/// Opens a resizable window that shows a `width` × `height` frame, scaled to fit
///
/// Returns `pl_result_not_supported` if there's no display to open one on (no Wayland or
/// X11 session): the caller runs headless instead. Logs why on any failure.
pl_result_e pl_display_open(pl_display_t *const display, const char *const title, const uint32_t width, const uint32_t height);

/// Shows `pixels`: `width` × `height` of 0x00RRGGBB, rows top to bottom; doesn't wait for vsync
void pl_display_present(pl_display_t *const display, const uint32_t *const pixels);

/// Closes the window
void pl_display_close(pl_display_t *const display);

//===----------------------------------------------------------------------===//
// Files
//===----------------------------------------------------------------------===//

/// Gets the size of the file at `path`
///
/// * `path`: Path of the file
/// * `out_size`: Receives the size in bytes
///
/// Returns `pl_result_io_error` if `path` is not a regular file
pl_result_e pl_file_size(const char *const path, size_t *const out_size);

/// Reads the whole file at `path` into `buf`
///
/// * `path`: Path of the file
/// * `buf`: Destination buffer, owned by the caller
/// * `cap`: Size of `buf` in bytes
/// * `out_len`: Receives the number of bytes read
///
/// Returns `pl_result_too_big` if the file does not fit in `cap` bytes
pl_result_e pl_file_read(const char *const path, void *const buf, const size_t cap, size_t *const out_len);

//===----------------------------------------------------------------------===//
// Memory
//===----------------------------------------------------------------------===//

/// Reserves `size` bytes of zeroed, read-write memory, aligned to a page
///
/// Pages only take up RAM once they are first touched, so a large reservation is cheap.
///
/// * `size`: Bytes to reserve, at least 1
/// * `out_ptr`: Receives the start of the memory
///
/// Returns `pl_result_out_of_memory` if the OS refuses
pl_result_e pl_mem_map(const size_t size, void **const out_ptr);

/// Returns memory from `pl_mem_map` to the OS
///
/// * `ptr`: What `pl_mem_map` returned
/// * `size`: The size passed to `pl_mem_map`
void pl_mem_unmap(void *const ptr, const size_t size);

//===----------------------------------------------------------------------===//
// Threads
//===----------------------------------------------------------------------===//

enum {
    // Linux limits thread names to 16 chars (including NULL)
    pl_thread_name_max_length = 16,
};

/// Opaque storage for a thread, owned by the caller
///
/// Each platform checks at compile time that its real thread type fits.
typedef struct pl_thread_t {
    union {
        uint64_t align;
        unsigned char bytes[64];
    } u;
} pl_thread_t;

/// Creates a new thread
///
/// * `thread`: Storage for the thread; must stay valid until `pl_thread_join`
/// * `thread_proc(void * arg)`: The thread function
/// * `arg`: Argument for the thread function
/// * `name`: Name shown in debuggers, or NULL; cut to `pl_thread_name_max_length - 1` chars
///
/// Returns `pl_result_io_error` if the OS refuses to create the thread
pl_result_e pl_thread_create(pl_thread_t *const thread, int (*const thread_proc)(void *arg), void *const arg, const char *const name);

/// Waits for `thread` to finish
///
/// Returns what the thread function returned
int pl_thread_join(pl_thread_t *const thread);

//===----------------------------------------------------------------------===//
// Mutexes
//===----------------------------------------------------------------------===//

/// Opaque storage for a mutex, owned by the caller
typedef struct pl_mutex_t {
    union {
        uint64_t align;
        unsigned char bytes[64];
    } u;
} pl_mutex_t;

/// Initializes a mutex; in debug builds, misuse (double lock, unlock by another thread) asserts
void pl_mutex_init(pl_mutex_t *const mutex);

/// Destroys a mutex
void pl_mutex_destroy(pl_mutex_t *const mutex);

/// Locks a mutex
void pl_mutex_lock(pl_mutex_t *const mutex);

/// Unlocks a mutex
void pl_mutex_unlock(pl_mutex_t *const mutex);

//===----------------------------------------------------------------------===//
// Atomics
//
// Header-only: atomics depend on the CPU and compiler, not the OS, and each call
// should compile to one instruction. The struct stops plain (non-atomic)
// arithmetic on the value.
//===----------------------------------------------------------------------===//

/// Atomic unsigned 32-bit integer
typedef struct pl_atomic_u32_t {
    uint32_t u32;
} pl_atomic_u32_t;

/// Memory ordering of an atomic operation (the C11 orders)
typedef enum pl_memory_order_e {
    pl_memory_order_relaxed = __ATOMIC_RELAXED,
    pl_memory_order_acquire = __ATOMIC_ACQUIRE,
    pl_memory_order_release = __ATOMIC_RELEASE,
    pl_memory_order_acq_rel = __ATOMIC_ACQ_REL,
    pl_memory_order_seq_cst = __ATOMIC_SEQ_CST,
} pl_memory_order_e;

/// Loads the value of `x`
///
/// * `x`: The atomic to load
/// * `order`: Must not be `pl_memory_order_release` or `pl_memory_order_acq_rel`
static inline uint32_t pl_atomic_load(pl_atomic_u32_t *const x, const pl_memory_order_e order) {
    assert(order != pl_memory_order_release && order != pl_memory_order_acq_rel);
    return __atomic_load_n(&x->u32, order);
}

/// Stores `value` into `x`
///
/// * `x`: The atomic to store to
/// * `value`: The new value
/// * `order`: Must not be `pl_memory_order_acquire` or `pl_memory_order_acq_rel`
static inline void pl_atomic_store(pl_atomic_u32_t *const x, const uint32_t value, const pl_memory_order_e order) {
    assert(order != pl_memory_order_acquire && order != pl_memory_order_acq_rel);
    __atomic_store_n(&x->u32, value, order);
}

/// Adds `value` to `x`, wrapping on overflow
///
/// Returns the value of `x` before the addition
static inline uint32_t pl_atomic_fetch_add(pl_atomic_u32_t *const x, const uint32_t value, const pl_memory_order_e order) {
    return __atomic_fetch_add(&x->u32, value, order);
}

/// Replaces the value of `x` with `value`
///
/// Returns the previous value of `x`
static inline uint32_t pl_atomic_exchange(pl_atomic_u32_t *const x, const uint32_t value, const pl_memory_order_e order) {
    return __atomic_exchange_n(&x->u32, value, order);
}

/// Compare-and-swap: if `x` equals `*expected`, sets `x` to `desired`
///
/// * `x`: The atomic to update
/// * `expected`: The value `x` must have; on failure, receives the current value of `x`
/// * `desired`: The new value
/// * `order`: Ordering on success. A failed CAS is only a load, so it uses `order` minus any release part
///
/// Returns true if `x` was updated
static inline bool pl_atomic_cas(pl_atomic_u32_t *const x, uint32_t *const expected, const uint32_t desired, const pl_memory_order_e order) {
    pl_memory_order_e failure_order = order;
    if (order == pl_memory_order_release) {
        failure_order = pl_memory_order_relaxed;
    } else if (order == pl_memory_order_acq_rel) {
        failure_order = pl_memory_order_acquire;
    }
    return __atomic_compare_exchange_n(&x->u32, expected, desired, false, order, failure_order);
}

#ifdef __cplusplus
}
#endif
