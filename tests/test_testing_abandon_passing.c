// CCCC_FLAGS: --testing --test-timeout=5
// CCCC_EXPECT_STDERR: ^ok 1 - starts
// CCCC_EXPECT_STDERR: ^worker finished
// CCCC_EXPECT_STDERR: ^ok 2 - waits
// A worker left running by a passing test is not stopped.
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

static void *worker(void *arg) {
    usleep(100000);
    puts("worker finished");
    return arg;
}

[[cccc::test]]
void starts(void) {
    pthread_t t;
    pthread_create(&t, 0, worker, 0);
}

[[cccc::test]]
void waits(void) {
    usleep(300000);
}
