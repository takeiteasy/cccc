// CCCC_FLAGS: --thread-safety
// CCCC_EXPECT_STDERR: DATA RACE DETECTED
// Detect an unsynchronized counter race: two threads increment a shared
// integer via pointer without holding any mutex.
#include <pthread.h>

static int *shared;

static void *inc(void *arg) {
    (void)arg;
    *shared = *shared + 1;
    return 0;
}

int main(void) {
    int x = 0;
    shared = &x;
    pthread_t a, b;
    if (pthread_create(&a, 0, inc, 0) != 0)
        return 1;
    if (pthread_create(&b, 0, inc, 0) != 0)
        return 2;
    pthread_join(a, 0);
    pthread_join(b, 0);
    return 42;
}
