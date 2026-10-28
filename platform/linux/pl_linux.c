//===-- pl_linux.c - Linux implementation of pl.h -------------------------===//
//
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// The only place (besides tests and pl_display_sdl.c) allowed to include OS
// headers.
//
//===----------------------------------------------------------------------===//

// -std=c99 hides POSIX from glibc's headers; this brings back POSIX 2008 + pthread_setname_np
#define _GNU_SOURCE

#include "platform/pl.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

// C99 has no static_assert: an array with negative size is a compile error
#define PL_STATIC_ASSERT(_name, _cond) typedef char pl_static_assert_##_name[(_cond) ? 1 : -1]

// For calls that return 0 on success and can only fail if the caller broke the contract
#define PL_VERIFY_ERR(_expr)   \
    do {                       \
        const int err = _expr; \
        assert(err == 0);      \
        (void)err;             \
    } while (0)

//===----------------------------------------------------------------------===//
// Time
//===----------------------------------------------------------------------===//

uint64_t pl_time_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
}

void pl_sleep_ms(const uint32_t ms) {
    struct timespec left = {(time_t)(ms / 1000), (long)(ms % 1000) * 1000000L};
    // a signal interrupts nanosleep early; it then stores the time still left in `left`
    while (nanosleep(&left, &left) == -1 && errno == EINTR) {
    }
}

void pl_sleep_until_ns(const uint64_t deadline_ns) {
    const struct timespec deadline = {(time_t)(deadline_ns / 1000000000u), (long)(deadline_ns % 1000000000u)};
    // the deadline is absolute, so after a signal the same call just goes back to sleep.
    // A deadline in the past returns at once.
    while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &deadline, NULL) == EINTR) {
    }
}

//===----------------------------------------------------------------------===//
// Events
//===----------------------------------------------------------------------===//

// Set by the signal handler, read by pl_tick; setting a sig_atomic_t is all a handler may safely do
static volatile sig_atomic_t g_quit;

// Set by pl_display_sdl.c while a window is open: drains the window's events, returns true
// for a quit. A pointer, so only programs that open a window link SDL.
bool (*pl_linux_display_poll)(void);

static void on_quit_signal(const int sig) {
    (void)sig;
    g_quit = 1;
}

size_t pl_tick(pl_event_t *const events, const size_t cap) {
    assert(cap >= 2);
    // ponytail: pl_tick is called from the main loop only, so a plain flag is enough
    static bool installed;
    if (!installed) {
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = on_quit_signal;
        // once: the next Ctrl-C gets the default action and kills the process
        sa.sa_flags = (int)SA_RESETHAND;
        sigemptyset(&sa.sa_mask);
        PL_VERIFY_ERR(sigaction(SIGINT, &sa, NULL));
        PL_VERIFY_ERR(sigaction(SIGTERM, &sa, NULL));
        installed = true;
    }

    // drained every tick, quit or not
    const bool display_quit = pl_linux_display_poll != NULL && pl_linux_display_poll();
    const uint64_t now = pl_time_now_ns();
    size_t count = 0;
    if (g_quit || display_quit) {
        g_quit = 0;
        events[count++] = (pl_event_t){pl_event_quit, now};
    }
    events[count++] = (pl_event_t){pl_event_time, now};
    return count;
}

//===----------------------------------------------------------------------===//
// Log
//===----------------------------------------------------------------------===//

void pl_log(const pl_log_severity_e severity, const char *const fmt, ...) {
    assert((unsigned)severity <= pl_log_severity_error);
    char buf[512];
    const uint64_t ms = pl_time_now_ns() / 1000000;
    const int n = snprintf(buf, sizeof(buf), "%c %llu.%03u ", "DIWE"[severity], (unsigned long long)(ms / 1000), (unsigned)(ms % 1000));

    // keep the last byte for '\n'
    const size_t cap = sizeof(buf) - 1 - (size_t)n;
    va_list args;
    va_start(args, fmt);
    const int m = vsnprintf(buf + n, cap, fmt, args);
    va_end(args);

    size_t len;
    if (m < 0) {
        len = (size_t)n;
    } else if ((size_t)m >= cap) {
        // cut off: vsnprintf wrote cap - 1 chars and a NUL
        len = (size_t)n + cap - 1;
        memcpy(buf + len - 3, "...", 3);
    } else {
        len = (size_t)n + (size_t)m;
    }
    buf[len++] = '\n';

    // One write per line: writes up to PIPE_BUF (4096) bytes to a pipe are never interleaved.
    // If stderr is gone there is nowhere to report that, so the result is ignored.
    if (write(STDERR_FILENO, buf, len) < 0) {
    }
}

//===----------------------------------------------------------------------===//
// Files
//===----------------------------------------------------------------------===//

static pl_result_e result_from_errno(void) {
    return (errno == ENOENT) ? pl_result_not_found : pl_result_io_error;
}

pl_result_e pl_file_size(const char *const path, size_t *const out_size) {
    struct stat st;
    if (stat(path, &st) != 0) {
        return result_from_errno();
    }
    if (!S_ISREG(st.st_mode)) {
        return pl_result_io_error;
    }
    *out_size = (size_t)st.st_size;
    return pl_result_ok;
}

pl_result_e pl_file_read(const char *const path, void *const buf, const size_t cap, size_t *const out_len) {
    assert(buf != NULL || cap == 0);
    const int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return result_from_errno();
    }

    pl_result_e result = pl_result_ok;
    size_t len = 0;
    for (;;) {
        // once buf is full, read one more byte to tell "exactly fits" from "too big"
        const bool full = (len == cap);
        char probe;
        const ssize_t n = full ? read(fd, &probe, 1) : read(fd, (char *)buf + len, cap - len);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n < 0) {
            result = pl_result_io_error;
            break;
        }
        if (n == 0) {
            break;
        }
        if (full) {
            result = pl_result_too_big;
            break;
        }
        len += (size_t)n;
    }
    close(fd);
    *out_len = len;
    return result;
}

//===----------------------------------------------------------------------===//
// Memory
//===----------------------------------------------------------------------===//

pl_result_e pl_mem_map(const size_t size, void **const out_ptr) {
    assert(size > 0);
    // the kernel rounds the length up to whole pages
    void *const ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ptr == MAP_FAILED) {
        return pl_result_out_of_memory;
    }
    *out_ptr = ptr;
    return pl_result_ok;
}

void pl_mem_unmap(void *const ptr, const size_t size) {
    PL_VERIFY_ERR(munmap(ptr, size));
}

//===----------------------------------------------------------------------===//
// Threads
//===----------------------------------------------------------------------===//

// What actually lives inside the caller's pl_thread_t bytes.
// The bytes are only ever accessed through this type, and only in this file.
typedef struct thread_context_t {
    pthread_t id;
    struct {
        int (*proc)(void *arg);
        void *arg;
    } callback;
} thread_context_t;

PL_STATIC_ASSERT(thread_fits, sizeof(thread_context_t) <= sizeof(pl_thread_t));
PL_STATIC_ASSERT(mutex_fits, sizeof(pthread_mutex_t) <= sizeof(pl_mutex_t));

// pthread wants void * (*)(void *); the thread function returns int
static void *pl_thread_proc(void *arg) {
    thread_context_t *const ctx = arg;
    return (void *)(intptr_t)ctx->callback.proc(ctx->callback.arg);
}

pl_result_e pl_thread_create(pl_thread_t *const thread, int (*const thread_proc)(void *arg), void *const arg, const char *const name) {
    thread_context_t *const ctx = (thread_context_t *)thread;
    ctx->callback.proc = thread_proc;
    ctx->callback.arg = arg;
    if (pthread_create(&ctx->id, NULL, pl_thread_proc, ctx) != 0) {
        return pl_result_io_error;
    }
    if (name != NULL) {
        char short_name[pl_thread_name_max_length];
        snprintf(short_name, sizeof(short_name), "%s", name);
        pthread_setname_np(ctx->id, short_name);
    }
    return pl_result_ok;
}

int pl_thread_join(pl_thread_t *const thread) {
    void *result;
    PL_VERIFY_ERR(pthread_join(((thread_context_t *)thread)->id, &result));
    return (int)(intptr_t)result;
}

//===----------------------------------------------------------------------===//
// Mutexes
//===----------------------------------------------------------------------===//

void pl_mutex_init(pl_mutex_t *const mutex) {
    pthread_mutexattr_t attr;
    PL_VERIFY_ERR(pthread_mutexattr_init(&attr));
#ifndef NDEBUG
    // Locking twice, or unlocking from the wrong thread, returns an error instead of
    // deadlocking or corrupting the lock, and PL_VERIFY_ERR turns that error into an assert.
    PL_VERIFY_ERR(pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK));
#endif
    PL_VERIFY_ERR(pthread_mutex_init((pthread_mutex_t *)mutex, &attr));
    PL_VERIFY_ERR(pthread_mutexattr_destroy(&attr));
}

void pl_mutex_destroy(pl_mutex_t *const mutex) {
    PL_VERIFY_ERR(pthread_mutex_destroy((pthread_mutex_t *)mutex));
}

void pl_mutex_lock(pl_mutex_t *const mutex) {
    PL_VERIFY_ERR(pthread_mutex_lock((pthread_mutex_t *)mutex));
}

void pl_mutex_unlock(pl_mutex_t *const mutex) {
    PL_VERIFY_ERR(pthread_mutex_unlock((pthread_mutex_t *)mutex));
}
