// CCCC_FLAGS: --testing --test-timeout=5
// CCCC_EXPECT_STDERR: ^not ok 1 - worker_asserts
// CCCC_EXPECT_STDERR: worker said no
// CCCC_EXPECT_STDERR: tss destructor ran
// CCCC_EXPECT_STDERR: ^ok 2 - after
// An Assert in a worker thread fails the test and ends the worker normally
// (its TSS destructors run); later tests still run.
#include <pthread.h>
#include <stdio.h>

static pthread_key_t key;

static void destructor(void *p) {
    puts("tss destructor ran");
}

static void *worker(void *arg) {
    pthread_setspecific(key, (void *)1);
    AssertMsg(0, "worker said no");
    return arg;
}

[[cccc::test]]
void worker_asserts(void) {
    pthread_t t;
    pthread_key_create(&key, destructor);
    pthread_create(&t, 0, worker, 0);
    pthread_join(t, 0);
}

[[cccc::test]]
void after(void) {
    Assert(1);
}
