//===-- pl_linux.c - Linux implementation of pl.h -------------------------===//
//
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// The only place (besides tests) allowed to include OS headers.
//
//===----------------------------------------------------------------------===//

// -std=c99 hides POSIX from glibc's headers; this brings back POSIX 2008 + pthread_setname_np
#define _GNU_SOURCE

#include "pl.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

// C99 has no static_assert: an array with negative size is a compile error
#define PL_STATIC_ASSERT(_name, _cond) typedef char pl_static_assert_##_name[(_cond) ? 1 : -1]

// For pthread calls that can only fail if the caller broke the contract
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

//===----------------------------------------------------------------------===//
// Log
//===----------------------------------------------------------------------===//

void pl_log(const pl_log_severity_e severity, const char * const fmt, ...) {
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

pl_result_e pl_file_size(const char * const path, size_t * const out_size) {
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

pl_result_e pl_file_read(const char * const path, void * const buf, const size_t cap, size_t * const out_len) {
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
// Threads
//===----------------------------------------------------------------------===//

// What actually lives inside the caller's pl_thread_t bytes.
// The bytes are only ever accessed through this type, and only in this file.
typedef struct thread_context_t {
    pthread_t id;
    struct {
        int (*proc)(void * arg);
        void * arg;
    } callback;
} thread_context_t;

PL_STATIC_ASSERT(thread_fits, sizeof(thread_context_t) <= sizeof(pl_thread_t));
PL_STATIC_ASSERT(mutex_fits, sizeof(pthread_mutex_t) <= sizeof(pl_mutex_t));

// pthread wants void * (*)(void *); the thread function returns int
static void * pl_thread_proc(void * arg) {
    thread_context_t * const ctx = arg;
    return (void *)(intptr_t)ctx->callback.proc(ctx->callback.arg);
}

pl_result_e pl_thread_create(pl_thread_t * const thread, int (*const thread_proc)(void * arg), void * const arg, const char * const name) {
    thread_context_t * const ctx = (thread_context_t *)thread;
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

int pl_thread_join(pl_thread_t * const thread) {
    void * result;
    PL_VERIFY_ERR(pthread_join(((thread_context_t *)thread)->id, &result));
    return (int)(intptr_t)result;
}

//===----------------------------------------------------------------------===//
// Mutexes
//===----------------------------------------------------------------------===//

void pl_mutex_init(pl_mutex_t * const mutex) {
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

void pl_mutex_destroy(pl_mutex_t * const mutex) {
    PL_VERIFY_ERR(pthread_mutex_destroy((pthread_mutex_t *)mutex));
}

void pl_mutex_lock(pl_mutex_t * const mutex) {
    PL_VERIFY_ERR(pthread_mutex_lock((pthread_mutex_t *)mutex));
}

void pl_mutex_unlock(pl_mutex_t * const mutex) {
    PL_VERIFY_ERR(pthread_mutex_unlock((pthread_mutex_t *)mutex));
}
