// CCCC_FLAGS: --testing --test-timeout=5
// CCCC_EXPECT_STDERR: ^ok 2 - threads
// A thread can be created and joined after an earlier test failed an Assert.
#include <pthread.h>

static void *worker(void *arg) {
    return arg;
}

[[cccc::test]]
void bad(void) {
    Assert(0);
}

[[cccc::test]]
void threads(void) {
    pthread_t t;
    void     *r = 0;
    Assert(pthread_create(&t, 0, worker, (void *)42) == 0);
    pthread_join(t, &r);
    Assert((long)r == 42);
}
