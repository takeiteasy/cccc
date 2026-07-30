// Expected return: 42
// #863: pthread_exit() called ON THE MAIN THREAD must run TSS/pthread-key
// destructors for values the main thread set, matching real glibc -- unlike
// a plain `return` from main(), which must NOT run them (see
// test_threads_tss.c / test_pthread_key_destructor.c for that side, and the
// <threads.h> row in docs/COVERAGE.md).
//
// main calls pthread_exit() with a deliberately WRONG status (3). If the
// destructor never runs, the process falls through with that wrong status
// and the test fails (exit 3 != 42). Only the destructor itself -- once it
// confirms it ran with the expected value -- overrides the process exit
// code to 42 via exit(). This makes "destructor didn't run" and "destructor
// ran with the wrong value" both observable failures, not false passes.
#include <pthread.h>
#include <stdlib.h>

static pthread_key_t g_key;

static void dtor(void *p) {
    int val = *(int *)p;
    free(p);
    exit(val == 55 ? 42 : 5);
}

int main(void) {
    if (pthread_key_create(&g_key, dtor) != 0)
        return 1;

    int *slot = malloc(sizeof(int));
    *slot = 55;
    if (pthread_setspecific(g_key, slot) != 0)
        return 2;

    pthread_exit((void *)3); // wrong on purpose -- see header comment
    return 3;                // unreachable
}
