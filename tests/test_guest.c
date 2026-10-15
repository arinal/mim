// SPDX-License-Identifier: MIT OR GPL-2.0-only

#define _GNU_SOURCE // dup, dup2, fileno

#include "guest.h"
#include "mim_test.h"

#include <string.h>
#include <unistd.h>

static mem_t mem;
static mem_budget_t budgets[2];
static mem_budget_t * const runtime = &budgets[0];
static mem_budget_t * const linear = &budgets[1];

// Reads build/<preset>/guests/<name>.wasm into a malloc'd buffer
static void * read_guest(const char * const name, size_t * const out_size) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.wasm", MIM_GUEST_DIR, name);
    size_t size;
    CHECK(pl_file_size(path, &size) == pl_result_ok);
    void * const data = malloc(size);
    CHECK(data != NULL);
    CHECK(pl_file_read(path, data, size, out_size) == pl_result_ok);
    return data;
}

// Loads `wasm`, ticks it 3 times, unloads it, and returns the "app:" and "guest:" lines it
// logged, without timestamps. Checks both budgets are empty afterwards.
static char * run(const void * const wasm, const size_t size, bool * const loaded) {
    // stderr goes to a file while the guest runs; it stays behind, so a sanitizer report
    // from a crash in here can still be read
    FILE * const log = fopen("test_guest.log", "w+");
    CHECK(log != NULL);
    fflush(stderr);
    const int saved = dup(STDERR_FILENO);
    CHECK(saved >= 0 && dup2(fileno(log), STDERR_FILENO) == STDERR_FILENO);

    guest_t guest;
    *loaded = guest_load(&guest, runtime, linear, wasm, size);
    for (uint64_t t = 16; t <= 48; t += 16) {
        guest_tick(&guest, t, 0.016f);
    }
    guest_unload(&guest);

    fflush(stderr);
    CHECK(dup2(saved, STDERR_FILENO) == STDERR_FILENO);
    close(saved);

    // nothing left behind: no wasm3 leaks, linear memory handed back
    CHECK(mem_get_stats(runtime).used_blocks == 0);
    mem_check(runtime);
    CHECK(mem_get_stats(linear).used == 0);

    // keep "app: ..." and "guest: ...", dropping "<severity> <seconds>.<ms> "
    static char line[1024];
    char * const out = calloc(64 * 1024, 1);
    CHECK(out != NULL);
    rewind(log);
    while (fgets(line, sizeof(line), log) != NULL) {
        const char * start = (strstr)(line, " app: ");
        if (start == NULL) {
            start = (strstr)(line, " guest: ");
        }
        if (start != NULL) {
            CHECK(strlen(out) + strlen(start) < 64 * 1024);
            strcat(out, start + 1);
        }
    }
    fclose(log);
    return out;
}

// Runs the guest called `name` and checks what guest_load returned (false for a trap in app_init too) and what it logged
static void expect(const char * const name, const bool should_load, const char * const expected) {
    size_t size;
    void * const wasm = read_guest(name, &size);
    bool loaded;
    char * const out = run(wasm, size, &loaded);
    if (loaded != should_load || (strstr)(out, expected) == NULL) {
        fprintf(stderr, "%s: expected %s and \"%s\" in:\n%s\n", name, should_load ? "loaded" : "rejected", expected, out);
        CHECK(false);
    }
    free(out);
    free(wasm);
}

static const char hello_log[] =
    "app: hello\n"
    "app: sum of 0..99999: 4999950000\n"
    "app: tick 1 at 16 ms, dt 16 ms\n"
    "app: tick 2 at 32 ms, dt 16 ms\n"
    "app: tick 3 at 48 ms, dt 16 ms\n"
    "app: bye after 3 ticks\n";

// The C++ and the Rust guest log exactly the same lines, and grow linear memory to do it
static void test_same_output(void) {
    const char * const names[] = {"hello_cpp", "hello_rust"};
    for (size_t i = 0; i < 2; i++) {
        size_t size;
        void * const wasm = read_guest(names[i], &size);
        bool loaded;
        char * const out = run(wasm, size, &loaded);
        if (!loaded || strcmp(out, hello_log) != 0) {
            fprintf(stderr, "%s logged:\n%s\nexpected:\n%s\n", names[i], out, hello_log);
            CHECK(false);
        }
        free(out);
        free(wasm);
    }
    // both start with 17 pages (a 1 MiB stack, then data); the 400 KB array needed more
    CHECK(mem_get_stats(linear).peak > 17 * 65536);
}

static void test_hostile(void) {
    expect("evil_bad_pointer", false, "guest: trapped in app_init: mim.log: buffer outside guest memory");
    expect("evil_wrapping_pointer", false, "guest: trapped in app_init: mim.log: buffer outside guest memory");
    expect("evil_bad_level", false, "guest: trapped in app_init: mim.log: bad level");
    // one line, not two
    expect("evil_log_injection", true, "app: ok?E 1.000 fake\napp: tick\n");
    expect("evil_recursion", false, "guest: trapped in app_init: [trap] stack overflow");
    expect("evil_unknown_import", false, "guest: trapped in app_init: function body is missing");
    expect("evil_missing_export", false, "guest: rejected: export 'app_tick': function lookup failed");
    expect("evil_wrong_signature", false, "guest: rejected: export 'app_tick' has the wrong signature");
    expect("evil_wrong_import", false, "guest: rejected: import 'mim.log': function signature mismatch");
    expect("evil_huge_memory", false, "guest: rejected: memory allocation failed");
    expect("evil_imported_memory", false, "guest: rejected: unallocated linear memory");

    // not a module at all, and a real one cut short
    unsigned char noise[256];
    for (size_t i = 0; i < sizeof(noise); i++) {
        noise[i] = (unsigned char)(i * 131 + 7);
    }
    bool loaded;
    char * out = run(noise, sizeof(noise), &loaded);
    CHECK(!loaded && (strstr)(out, "guest: rejected: ") != NULL);
    free(out);

    size_t size;
    void * const wasm = read_guest("hello_rust", &size);
    out = run(wasm, size / 2, &loaded);
    CHECK(!loaded && (strstr)(out, "guest: rejected: ") != NULL);
    free(out);
    free(wasm);
}

// A guest takes all of linear memory and dirties it; the next one still gets zeroed pages.
// 8 MiB is 128 pages, minus the 17 it starts with and the block's header.
static void test_greedy_twice(void) {
    expect("evil_greedy", true, "app: grew 0110 pages, all zero\napp: tick\n");
    expect("evil_greedy", true, "app: grew 0110 pages, all zero\napp: tick\n");
    CHECK(mem_get_stats(linear).peak <= mem_get_stats(linear).size);
}

int main(void) {
    const mem_budget_def_t defs[] = {
        {"guest runtime", 8 * 1024 * 1024, mem_kind_heap},
        {"guest linear", 8 * 1024 * 1024, mem_kind_bump},
    };
    CHECK(mem_init(&mem, defs, budgets, 2) == pl_result_ok);

    test_same_output();
    test_hostile();
    test_greedy_twice();
    // and a normal guest still runs after all of that
    test_same_output();

    CHECK(mem_shutdown(&mem) == 0);
    return 0;
}
