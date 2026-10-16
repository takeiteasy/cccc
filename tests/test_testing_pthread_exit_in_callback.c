// CCCC_FLAGS: --testing --test-timeout=5
// CCCC_REJECT_STDOUT: after qsort
// CCCC_EXPECT_STDERR: ^ok 1 - exit_in_comparator
// pthread_exit inside a qsort comparator on a worker ends the worker.
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

static int cmp(const void *a, const void *b) {
    pthread_exit((void *)42);
    return 0;
}

static void *worker(void *arg) {
    int v[3] = {3, 1, 2};
    qsort(v, 3, sizeof v[0], cmp);
    puts("after qsort");
    return 0;
}

[[cccc::test]]
void exit_in_comparator(void) {
    pthread_t t;
    void     *ret = 0;
    pthread_create(&t, 0, worker, 0);
    pthread_join(t, &ret);
    Assert((long)ret == 42);
}
