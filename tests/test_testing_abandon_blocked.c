// CCCC_NATIVE_SKIP: --testing=native refuses --test-timeout
// CCCC_FLAGS: --testing --test-timeout=5
// CCCC_EXPECT_STDERR: ^not ok 1 - fails
// CCCC_EXPECT_STDERR: ^# test 1: 1 guest thread\(s\) still blocked
// CCCC_EXPECT_STDERR: ^ok 2 - after
// A worker blocked for good on a condition does not hang the run; the harness
// reports it and moves on.
#include <pthread.h>
#include <unistd.h>

static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  cv = PTHREAD_COND_INITIALIZER;

static void *waiter(void *arg) {
    pthread_mutex_lock(&mu);
    pthread_cond_wait(&cv, &mu);
    pthread_mutex_unlock(&mu);
    return arg;
}

[[cccc::test]]
void fails(void) {
    pthread_t t;
    pthread_create(&t, 0, waiter, 0);
    usleep(20000);
    Assert(0);
}

[[cccc::test]]
void after(void) {
    Assert(1);
}
