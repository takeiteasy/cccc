// CCCC_FLAGS: --testing --test-timeout=5
// CCCC_EXPECT_STDERR: ^not ok 1 - fails
// CCCC_EXPECT_STDERR: ^ok 2 - after
// A worker released to spin as its test fails is stopped: it neither holds
// the GIL against later tests nor keeps running during them.
#include <pthread.h>
#include <unistd.h>

static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  cv = PTHREAD_COND_INITIALIZER;
static int             go;
static volatile long   counter;

static void *spin(void *arg) {
    pthread_mutex_lock(&mu);
    while (!go)
        pthread_cond_wait(&cv, &mu);
    pthread_mutex_unlock(&mu);
    for (;;)
        counter++;
    return arg;
}

[[cccc::test]]
void fails(void) {
    pthread_t t;
    pthread_create(&t, 0, spin, 0);
    usleep(20000);
    pthread_mutex_lock(&mu);
    go = 1;
    pthread_cond_signal(&cv);
    pthread_mutex_unlock(&mu);
    Assert(0);
}

[[cccc::test]]
void after(void) {
    long before = counter;
    usleep(50000);
    Assert(counter == before);
}
