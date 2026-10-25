// SPDX-License-Identifier: MIT OR GPL-2.0-only

#define _GNU_SOURCE // pipe, dup, dup2

#include "mim_test.h"
#include "platform/pl.h"

#include <string.h>
#include <unistd.h>

// Runs pl_log with stderr pointed at a pipe and returns what came out.
static size_t capture(char * const out, const size_t cap, const pl_log_severity_e severity, const char * const msg) {
    int fds[2];
    CHECK(pipe(fds) == 0);
    const int saved = dup(STDERR_FILENO);
    CHECK(saved >= 0);
    CHECK(dup2(fds[1], STDERR_FILENO) == STDERR_FILENO);
    pl_log(severity, "%s", msg);
    CHECK(dup2(saved, STDERR_FILENO) == STDERR_FILENO);
    close(saved);
    close(fds[1]);

    const ssize_t n = read(fds[0], out, cap - 1);
    close(fds[0]);
    CHECK(n > 0);
    out[n] = '\0';
    return (size_t)n;
}

static bool ends_with(const char * const s, const size_t len, const char * const suffix) {
    const size_t n = strlen(suffix);
    return len >= n && memcmp(s + len - n, suffix, n) == 0;
}

int main(void) {
    char out[1024];

    size_t len = capture(out, sizeof(out), pl_log_severity_info, "hello");
    unsigned long long seconds;
    unsigned ms;
    CHECK(sscanf(out, "I %llu.%u ", &seconds, &ms) == 2);
    CHECK(ms < 1000);
    CHECK(ends_with(out, len, " hello\n"));
    // exactly one line. (strchr) skips glibc's _Generic strchr macro, which clang -pedantic
    // rejects in C99 mode.
    CHECK((strchr)(out, '\n') == out + len - 1);

    capture(out, sizeof(out), pl_log_severity_debug, "x");
    CHECK(out[0] == 'D');
    capture(out, sizeof(out), pl_log_severity_warn, "x");
    CHECK(out[0] == 'W');
    capture(out, sizeof(out), pl_log_severity_error, "x");
    CHECK(out[0] == 'E');

    char big[2000];
    memset(big, 'x', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';
    len = capture(out, sizeof(out), pl_log_severity_info, big);
    CHECK(len == 511);
    CHECK(ends_with(out, len, "xxx...\n"));
    return 0;
}
