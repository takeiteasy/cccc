// Expected return: 42
// #1278: pthread_once is declared in the bundled <pthread.h> (so CCCC can
// parse its own source), but the VM had no FFI wrapper for it -- guest code
// calling it under the VM got "undefined function: pthread_once". This
// exercises wrap_pthread_once (src/stdlib/pthread.c): several threads race on
// one control object; the initializer must run exactly once, and every
// caller must observe it as already run. A final single-threaded call after
// join must still not re-run it. Sibling of test_threads_call_once_1088.c.
#include <pthread.h>
#include <stdatomic.h>

#define NUM_THREADS 8

static pthread_once_t g_once       = PTHREAD_ONCE_INIT;
static _Atomic int    g_init_calls = 0;
static _Atomic int    g_saw_init   = 0;

static void do_init(void) {
    atomic_fetch_add(&g_init_calls, 1);
}

static void *worker(void *arg) {
    (void)arg;
    pthread_once(&g_once, do_init);
    if (atomic_load(&g_init_calls) >= 1)
        atomic_fetch_add(&g_saw_init, 1);
    return 0;
}

int main(void) {
    pthread_t threads[NUM_THREADS];

    for (int i = 0; i < NUM_THREADS; i++)
        if (pthread_create(&threads[i], 0, worker, 0) != 0)
            return 1;
    for (int i = 0; i < NUM_THREADS; i++)
        pthread_join(threads[i], 0);

    pthread_once(&g_once, do_init);

    if (atomic_load(&g_init_calls) != 1)
        return 2;
    if (atomic_load(&g_saw_init) != NUM_THREADS)
        return 3;

    return 42;
}
