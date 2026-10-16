// CCCC_FLAGS: --testing --test-timeout=5
// CCCC_EXPECT_STDERR: ^not ok 1 - fails
// CCCC_EXPECT_STDERR: ^ok 2 - after
// A worker asleep when its test fails ends on waking; it never writes into a
// later test's state.
#include <pthread.h>
#include <unistd.h>

static volatile int woke;

static void *sleeper(void *arg) {
    usleep(200000);
    woke = 1;
    return arg;
}

[[cccc::test]]
void fails(void) {
    pthread_t t;
    pthread_create(&t, 0, sleeper, 0);
    usleep(20000);
    Assert(0);
}

[[cccc::test]]
void after(void) {
    usleep(400000);
    Assert(!woke);
}
