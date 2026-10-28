// SPDX-License-Identifier: MIT OR GPL-2.0-only

#include "mim_test.h"
#include "platform/pl.h"

enum {
    thread_count = 8,
    increments_per_thread = 100000,
};

static pl_atomic_u32_t counter;

static int increment(void *const arg) {
    (void)arg;
    for (int i = 0; i < increments_per_thread; ++i) {
        pl_atomic_fetch_add(&counter, 1, pl_memory_order_relaxed);
    }
    return 0;
}

// Message passing: data is a plain int. The release store / acquire load on `ready` is the only
// thing that makes the consumer guaranteed to see data == 42.
static int data;
static pl_atomic_u32_t ready;

static int producer(void *const arg) {
    (void)arg;
    data = 42;
    pl_atomic_store(&ready, 1, pl_memory_order_release);
    return 0;
}

int main(void) {
    pl_atomic_u32_t a = {5};
    CHECK(pl_atomic_load(&a, pl_memory_order_relaxed) == 5);
    pl_atomic_store(&a, 7, pl_memory_order_release);
    CHECK(pl_atomic_load(&a, pl_memory_order_acquire) == 7);
    CHECK(pl_atomic_fetch_add(&a, 3, pl_memory_order_seq_cst) == 7);
    CHECK(pl_atomic_load(&a, pl_memory_order_seq_cst) == 10);
    CHECK(pl_atomic_exchange(&a, 1, pl_memory_order_acq_rel) == 10);
    CHECK(pl_atomic_load(&a, pl_memory_order_relaxed) == 1);

    uint32_t expected = 1;
    CHECK(pl_atomic_cas(&a, &expected, 2, pl_memory_order_acq_rel));
    CHECK(pl_atomic_load(&a, pl_memory_order_relaxed) == 2);
    expected = 1;
    CHECK(!pl_atomic_cas(&a, &expected, 3, pl_memory_order_release));
    CHECK(expected == 2); // a failed CAS reports the current value
    CHECK(pl_atomic_load(&a, pl_memory_order_relaxed) == 2);

    pl_atomic_store(&a, UINT32_MAX, pl_memory_order_relaxed);
    CHECK(pl_atomic_fetch_add(&a, 1, pl_memory_order_relaxed) == UINT32_MAX);
    CHECK(pl_atomic_load(&a, pl_memory_order_relaxed) == 0); // wraps

    // stress: no mutex, and still no lost increments
    pl_thread_t threads[thread_count];
    for (int i = 0; i < thread_count; ++i) {
        CHECK(pl_thread_create(&threads[i], increment, NULL, "mim-inc") == pl_result_ok);
    }
    for (int i = 0; i < thread_count; ++i) {
        CHECK(pl_thread_join(&threads[i]) == 0);
    }
    CHECK(pl_atomic_load(&counter, pl_memory_order_relaxed) == (uint32_t)thread_count * increments_per_thread);

    pl_thread_t t;
    CHECK(pl_thread_create(&t, producer, NULL, "mim-producer") == pl_result_ok);
    while (!pl_atomic_load(&ready, pl_memory_order_acquire)) {
    }
    CHECK(data == 42);
    CHECK(pl_thread_join(&t) == 0);
    return 0;
}
