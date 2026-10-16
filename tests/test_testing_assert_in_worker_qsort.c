// CCCC_FLAGS: --testing --test-timeout=5
// CCCC_EXPECT_STDERR: ^not ok 1 - worker_qsort
// CCCC_EXPECT_STDERR: comparator said no
// CCCC_EXPECT_STDERR: ^ok 2 - after
// CCCC_EXPECT_STDOUT: tss destructor ran
// CCCC_REJECT_STDOUT: after qsort
// An Assert in a qsort comparator on a worker ends the whole worker, not just
// the comparator; its TSS destructors still run.
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

static pthread_key_t key;

static void destructor(void *p) {
    puts("tss destructor ran");
}

static int cmp(const void *a, const void *b) {
    AssertMsg(0, "comparator said no");
    return *(const int *)a - *(const int *)b;
}

static void *worker(void *arg) {
    int v[3] = {3, 1, 2};
    pthread_setspecific(key, (void *)1);
    qsort(v, 3, sizeof v[0], cmp);
    puts("after qsort");
    return arg;
}

[[cccc::test]]
void worker_qsort(void) {
    pthread_t t;
    pthread_key_create(&key, destructor);
    pthread_create(&t, 0, worker, 0);
    pthread_join(t, 0);
}

[[cccc::test]]
void after(void) {
    Assert(1);
}
