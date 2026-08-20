// Expected return: 42
// Each thread sees its own copy of a _Thread_local variable.
#include <threads.h>
#include <stdatomic.h>

_Thread_local int  g_tls      = 0;
static _Atomic int g_failures = 0;

static int worker(void *arg) {
    int id = *(int *)arg;
    g_tls  = id * 10;
    // Let another thread potentially run
    thrd_yield();
    if (g_tls != id * 10)
        atomic_fetch_add(&g_failures, 1);
    return 0;
}

int main(void) {
    thrd_t threads[4];
    int    ids[4];
    for (int i = 0; i < 4; i++) {
        ids[i] = i + 1;
        if (thrd_create(&threads[i], worker, &ids[i]) != thrd_success)
            return 1;
    }
    for (int i = 0; i < 4; i++)
        thrd_join(threads[i], 0);

    return atomic_load(&g_failures) == 0 ? 42 : 2;
}
