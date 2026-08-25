// Expected return: 42
// #1184: atomic_fetch_add/sub/or/xor/and used to expand to a plain, non-
// atomic load-then-store ("_old = load(obj); store(obj, _old + val)") --
// correct only under the VM's own GIL (bytecode execution is uninterruptible
// between instructions there), but a genuine data race once -c=native gives
// real thread parallelism: concurrent threads could load the same _old and
// each store _old+val, silently losing updates. Found stress-testing
// tests/test_threads_call_once_1088.c (~7-13% failure rate under --native).
// Fixed by rewriting the macros as a real CAS retry loop (include/
// stdatomic.h), the same shape to_assign() already builds for `_Atomic x +=
// y`. This test drives the actual race directly, rather than through
// call_once, so a regression here doesn't need a call_once-shaped repro to
// surface: many threads hammer one atomic counter with fetch_add/fetch_sub,
// and a separate counter with fetch_or/fetch_xor/fetch_and whose final value
// is order-independent regardless of interleaving.
#include <threads.h>
#include <stdatomic.h>

#define NUM_THREADS      8
#define ITERS_PER_THREAD 2000

static _Atomic int g_counter = 0;
static _Atomic int g_bits    = 0;

static int adder(void *arg) {
    (void)arg;
    for (int i = 0; i < ITERS_PER_THREAD; i++)
        atomic_fetch_add(&g_counter, 1);
    return 0;
}

static int suber(void *arg) {
    (void)arg;
    for (int i = 0; i < ITERS_PER_THREAD; i++)
        atomic_fetch_sub(&g_counter, 1);
    return 0;
}

// Each thread ORs in its own bit, then XORs it back out, then ANDs a mask
// that always includes every thread's bit -- order-independent: whichever
// interleaving happens, the AND above never actually clears anything real
// (the mask keeps every bit set), so the net effect of the OR/XOR pair
// always cancels once every thread finishes its own pair. If fetch_or/
// fetch_xor/fetch_and raced and lost an update, g_bits would end up nonzero.
static int bit_worker(void *arg) {
    int bit = (int)(long long)arg;
    atomic_fetch_or(&g_bits, 1 << bit);
    atomic_fetch_and(&g_bits, ~0);
    atomic_fetch_xor(&g_bits, 1 << bit);
    return 0;
}

int main(void) {
    thrd_t threads[NUM_THREADS];

    // Half the threads add, half subtract -- net effect on g_counter is 0,
    // but only if every fetch_add/fetch_sub is a real atomic RMW: a lost
    // update in either direction shows up directly in the final value.
    for (int i = 0; i < NUM_THREADS; i++) {
        int (*fn)(void *) = (i % 2 == 0) ? adder : suber;
        if (thrd_create(&threads[i], fn, NULL) != thrd_success)
            return 1;
    }
    for (int i = 0; i < NUM_THREADS; i++)
        thrd_join(threads[i], 0);

    if (atomic_load(&g_counter) != 0)
        return 2;

    for (int i = 0; i < NUM_THREADS; i++)
        if (thrd_create(&threads[i], bit_worker, (void *)(long long)i) !=
            thrd_success)
            return 3;
    for (int i = 0; i < NUM_THREADS; i++)
        thrd_join(threads[i], 0);

    if (atomic_load(&g_bits) != 0)
        return 4;

    return 42;
}
