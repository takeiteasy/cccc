// CCCC_FLAGS: --testing --test-timeout=5
// CCCC_EXPECT_STDERR: ^not ok 1 - fails
// CCCC_EXPECT_STDERR: ^ok 2 - after
// CCCC_REJECT_STDERR: dtor ran
// An abandoned worker skips its TSS destructors.
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

static pthread_key_t key;

static void dtor(void *p) {
    puts("dtor ran");
}

static void *worker(void *arg) {
    pthread_setspecific(key, (void *)1);
    usleep(100000);
    return arg;
}

[[cccc::test]]
void fails(void) {
    pthread_t t;
    pthread_key_create(&key, dtor);
    pthread_create(&t, 0, worker, 0);
    usleep(20000);
    Assert(0);
}

[[cccc::test]]
void after(void) {
    usleep(300000);
    Assert(1);
}
