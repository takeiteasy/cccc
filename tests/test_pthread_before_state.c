// CCCC_FLAGS: --testing
// CCCC_EXPECT_STDERR: ^ok 1 - join_worker_created_first
// A worker created before the first pthread_self call is still tracked, so
// joining it succeeds.
#include <pthread.h>

static void *worker(void *arg) {
    return arg;
}

[[cccc::test]]
void join_worker_created_first(void) {
    pthread_t t;
    void     *ret = 0;
    Assert(pthread_create(&t, 0, worker, (void *)42) == 0);
    Assert(pthread_self() != 0);
    Assert(pthread_join(t, &ret) == 0);
    Assert((long)ret == 42);
}
