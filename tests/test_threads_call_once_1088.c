// Expected return: 42
// #1088: call_once used to be a guest-side macro (a plain, non-atomic
// `if (!*flag) { *flag = 1; func(); }`), safe only under the VM's own GIL --
// bytecode execution is serialized there, so the flag check and the call
// were atomic *from the VM's perspective* even though the C source wasn't.
// Under -c=native there is real parallelism and no such guarantee, so
// call_once is now a real function (VM cfunc + -c=native shim), backed by an
// atomic compare-exchange on the flag on both backends. Several threads race
// on one once_flag here; the initializer must run exactly once regardless.
#include <threads.h>
#include <stdatomic.h>

#define NUM_THREADS 8

static once_flag   g_once       = ONCE_FLAG_INIT;
static _Atomic int g_init_calls = 0;
static _Atomic int g_saw_init   = 0;

static void do_init(void) {
    atomic_fetch_add(&g_init_calls, 1);
}

static int worker(void *arg) {
    (void)arg;
    call_once(&g_once, do_init);
    // Every worker must observe the initializer as having already run --
    // call_once only guarantees ordering (7.26.6.2p2: completion of func
    // synchronizes-with every later call_once on the same flag), so this
    // check is valid for every caller, not just the one that ran it.
    if (atomic_load(&g_init_calls) >= 1)
        atomic_fetch_add(&g_saw_init, 1);
    return 0;
}

int main(void) {
    thrd_t threads[NUM_THREADS];

    for (int i = 0; i < NUM_THREADS; i++)
        if (thrd_create(&threads[i], worker, NULL) != thrd_success)
            return 1;
    for (int i = 0; i < NUM_THREADS; i++)
        thrd_join(threads[i], 0);

    // A second, purely single-threaded round after all workers have joined --
    // still must not run the initializer again.
    call_once(&g_once, do_init);

    if (atomic_load(&g_init_calls) != 1)
        return 2;
    if (atomic_load(&g_saw_init) != NUM_THREADS)
        return 3;

    return 42;
}
