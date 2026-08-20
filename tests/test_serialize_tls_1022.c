// Expected return: 42
// #1022: Obj.is_tls (_Thread_local/__thread) was parsed and tracked but
// never re-emitted by serialize.c -- a _Thread_local global silently
// serialized as an ordinary global under -c=native/-m, so every thread
// shared one instance instead of getting its own copy. Confirmed failing
// pre-fix: each worker below would observe another thread's write.
//
// Deliberately uses <pthread.h>, not <threads.h> -- the latter has no
// -c=native lowering at all yet (VM cfuncs with no host libc symbol,
// #1088), so it can't round-trip and would be excluded from the native
// suite regardless of what this test is trying to check.
#include <pthread.h>
#include <stdatomic.h>

_Thread_local int g_tls = 0;
static _Atomic int g_failures = 0;

static void *worker(void *arg) {
    int id = *(int *)arg;
    g_tls = id * 10;
    // Give another thread a chance to run and clobber a shared (non-TLS)
    // instance before this thread reads its own value back.
    for (volatile int i = 0; i < 1000000; i++)
        ;
    if (g_tls != id * 10)
        atomic_fetch_add(&g_failures, 1);
    return 0;
}

int main(void) {
    pthread_t threads[4];
    int ids[4];
    for (int i = 0; i < 4; i++) {
        ids[i] = i + 1;
        if (pthread_create(&threads[i], 0, worker, &ids[i]) != 0)
            return 1;
    }
    for (int i = 0; i < 4; i++)
        pthread_join(threads[i], 0);

    return atomic_load(&g_failures) == 0 ? 42 : 2;
}
