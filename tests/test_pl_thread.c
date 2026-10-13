// SPDX-License-Identifier: MIT OR GPL-2.0-only

#include "mim_test.h"
#include "pl.h"

enum {
    thread_count = 8,
    increments_per_thread = 100000,
};

static int return_arg(void * const arg) {
    return (int)(intptr_t)arg;
}

static pl_mutex_t lock;
static uint64_t counter;

static int increment(void * const arg) {
    (void)arg;
    for (int i = 0; i < increments_per_thread; ++i) {
        pl_mutex_lock(&lock);
        counter++;
        pl_mutex_unlock(&lock);
    }
    return 0;
}

int main(void) {
    pl_thread_t t;
    CHECK(pl_thread_create(&t, return_arg, (void *)(intptr_t)42, "mim-ret") == pl_result_ok);
    CHECK(pl_thread_join(&t) == 42);
    CHECK(pl_thread_create(&t, return_arg, (void *)(intptr_t)-7, NULL) == pl_result_ok);
    CHECK(pl_thread_join(&t) == -7);
    // longer than Linux's 15-character limit: must be cut, not fail
    CHECK(pl_thread_create(&t, return_arg, NULL, "a-thread-name-over-fifteen") == pl_result_ok);
    CHECK(pl_thread_join(&t) == 0);

    // stress: every increment must survive 8 threads fighting over the lock
    pl_mutex_init(&lock);
    pl_thread_t threads[thread_count];
    for (int i = 0; i < thread_count; ++i) {
        CHECK(pl_thread_create(&threads[i], increment, NULL, "mim-inc") == pl_result_ok);
    }
    for (int i = 0; i < thread_count; ++i) {
        CHECK(pl_thread_join(&threads[i]) == 0);
    }
    CHECK(counter == (uint64_t)thread_count * increments_per_thread);
    pl_mutex_destroy(&lock);
    return 0;
}
