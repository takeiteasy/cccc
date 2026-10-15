// CCCC_FLAGS: --testing --test-timeout=5
// CCCC_EXPECT_STDERR: worker failed first
// CCCC_REJECT_STDERR: main failed second
// The first failing Assert's message is the one reported.
#include <pthread.h>

static void *worker(void *arg) {
    AssertMsg(0, "worker failed first");
    return arg;
}

[[cccc::test]]
void both_fail(void) {
    pthread_t t;
    pthread_create(&t, 0, worker, 0);
    pthread_join(t, 0);
    AssertMsg(0, "main failed second");
}
