// CCCC_NATIVE_SKIP: --testing=native refuses --test-timeout
// CCCC_FLAGS: --testing --test-timeout=1
// CCCC_EXPECT_STDERR: ^not ok 1 - hangs # TIMEOUT
// CCCC_EXPECT_STDERR: ^ok 2 - after
// A test that times out leaves its still-running worker behind no longer.
#include <pthread.h>
#include <unistd.h>

static volatile long counter;

static void *loopy(void *arg) {
    for (;;) {
        counter++;
        usleep(1000);
    }
    return arg;
}

[[cccc::test]]
void hangs(void) {
    pthread_t t;
    pthread_create(&t, 0, loopy, 0);
    for (;;)
        usleep(1000);
}

[[cccc::test]]
void after(void) {
    long before = counter;
    usleep(50000);
    Assert(counter == before);
}
