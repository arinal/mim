// SPDX-License-Identifier: MIT OR GPL-2.0-only

#define _GNU_SOURCE // fork, pipe, dup2

#include "mim_test.h"
#include "runtime/mem/mem.h"

#include <signal.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

static mem_t mem;
static mem_budget_t budgets[2];
static mem_budget_t *const heap = &budgets[0];
static mem_budget_t *const bump = &budgets[1];

typedef enum death_e {
    // mem.c's own check: pl_log, then abort()
    death_abort,
    // AddressSanitizer stopped the access first: it reports and exits with 1
    death_asan,
} death_e;

// Runs `fn` in a child process, with stderr through a pipe, and checks how it died and what it printed
static void expect_death(void (*const fn)(void), const death_e how, const char *const expected) {
    int fds[2];
    CHECK(pipe(fds) == 0);
    const pid_t pid = fork();
    CHECK(pid >= 0);
    if (pid == 0) {
        // no core dump: dumping is slow, and these deaths are expected
        const struct rlimit no_core = {0, 0};
        setrlimit(RLIMIT_CORE, &no_core);
        dup2(fds[1], STDERR_FILENO);
        close(fds[0]);
        close(fds[1]);
        fn();
        _exit(0); // fn should not get here
    }
    close(fds[1]);

    // keep reading to the end, so a long ASan report can't block the child
    static char out[64 * 1024];
    size_t len = 0;
    ssize_t n;
    while ((n = read(fds[0], out + len, sizeof(out) - 1 - len)) > 0) {
        len += (size_t)n;
        if (len == sizeof(out) - 1) {
            len = 0;
        }
    }
    out[len] = '\0';
    close(fds[0]);

    int status;
    CHECK(waitpid(pid, &status, 0) == pid);
    if (how == death_abort) {
        CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
    } else {
        CHECK(WIFEXITED(status) && WEXITSTATUS(status) != 0);
    }
    // (strstr) skips glibc's _Generic strstr macro, which clang -pedantic rejects in C99 mode
    if ((strstr)(out, expected) == NULL) {
        fprintf(stderr, "expected \"%s\" in:\n%s\n", expected, out);
        CHECK(false);
    }
}

static void over_budget(void) {
    mem_alloc(heap, 5000, "too_big");
}

static void double_free(void) {
    void *const p = mem_alloc(heap, 10, "twice");
    mem_free(heap, p);
    mem_free(heap, p);
}

static void double_free_merged(void) {
    void *const a = mem_alloc(heap, 10, "a");
    void *const b = mem_alloc(heap, 10, "b");
    mem_alloc(heap, 10, "keep"); // so b merges only with a, not with the free tail
    mem_free(heap, a);
    mem_free(heap, b); // b's header is now inside a's free block
    mem_free(heap, b);
}

static void foreign_pointer(void) {
    int x;
    mem_free(heap, &x);
}

static void free_on_bump(void) {
    mem_free(bump, mem_alloc(bump, 10, "bump"));
}

static void reset_on_heap(void) {
    mem_reset(heap);
}

static void grow_not_last(void) {
    void *const p = mem_alloc(bump, 10, "first");
    mem_alloc(bump, 10, "second");
    mem_try_grow(bump, p, 20);
}

static void damaged_guard(void) {
    unsigned char *const p = mem_alloc(heap, 13, "short");
    p[13] = 0; // one past the end
    mem_free(heap, p);
}

static void damaged_magic(void) {
    unsigned char *const p = mem_alloc(heap, 16, "under");
    p[-1] = 0; // one before the start: the last byte of the header's magic
    mem_free(heap, p);
}

static void damaged_free_list(void) {
    unsigned char *const a = mem_alloc(heap, 16, "a");
    unsigned char *const keep = mem_alloc(heap, 16, "keep");
    mem_free(heap, a);
    // the 32-byte header's next_free sits 16 bytes before the data; point it at a used block's header
    unsigned char *const used_header = keep - 32;
    memcpy(a - 16, &used_header, sizeof(used_header));
    mem_alloc(heap, 100, "walks past a");
}

static void write_after_free(void) {
    unsigned char *const p = mem_alloc(heap, 16, "stale");
    mem_free(heap, p);
    p[0] = 0;
    mem_check(heap);
}

// The checker itself: each case breaks one rule of the heap behind the allocator's back

// Marks the used block at `p` as free: its header's magic is the 4 bytes before the data
static void fake_free(unsigned char *const p) {
    const uint32_t magic_free = 0xF4EEB10Cu;
    memcpy(p - 4, &magic_free, sizeof(magic_free));
}

static void check_unmerged(void) {
    unsigned char *const a = mem_alloc(heap, 16, "a");
    unsigned char *const b = mem_alloc(heap, 16, "b");
    mem_alloc(heap, 16, "keep");
    mem_free(heap, a);
    fake_free(b); // two free blocks in a row
    mem_check(heap);
}

static void check_off_free_list(void) {
    mem_alloc(heap, 16, "a");
    fake_free(mem_alloc(heap, 16, "b")); // after a used block, but not on the free list
    mem_check(heap);
}

static void check_counters(void) {
    mem_alloc(heap, 16, "a");
    heap->internal.used_blocks++; // the blocks are fine, the counter isn't
    mem_check(heap);
}

int main(void) {
    const mem_budget_def_t defs[] = {
        {"heap", 4096, mem_kind_heap},
        {"bump", 4096, mem_kind_bump},
    };
    CHECK(mem_init(&mem, defs, budgets, 2) == pl_result_ok);

    expect_death(over_budget, death_abort, "mem: budget 'heap' is full: wanted 5000 bytes (tag 'too_big')");
    expect_death(over_budget, death_abort, "largest free block");
    expect_death(double_free, death_abort, "double free");
    expect_death(double_free_merged, death_abort, "double free after a merge");
    expect_death(damaged_free_list, death_abort, "free list damaged");
    expect_death(foreign_pointer, death_abort, "is not from this budget");
    expect_death(free_on_bump, death_abort, "is a bump budget");
    expect_death(reset_on_heap, death_abort, "is a heap budget");
    expect_death(grow_not_last, death_abort, "not the last allocation");
    expect_death(damaged_magic, death_abort, "is not a live block");
    expect_death(check_unmerged, death_abort, "was not merged with the one before it");
    expect_death(check_off_free_list, death_abort, "free list doesn't match the heap");
    expect_death(check_counters, death_abort, "counters don't match the heap");

    // With ASan, the bad write itself is stopped, before mem.c's check runs
#ifdef __SANITIZE_ADDRESS__
    expect_death(damaged_guard, death_asan, "use-after-poison");
    expect_death(write_after_free, death_asan, "use-after-poison");
#else
    expect_death(damaged_guard, death_abort, "guard bytes after a 13-byte block damaged (tag 'short')");
    expect_death(write_after_free, death_abort, "written after free (last tag 'stale')");
#endif

    // the children's mistakes stayed in the children
    mem_check(heap);
    CHECK(mem_shutdown(&mem) == 0);
    return 0;
}
