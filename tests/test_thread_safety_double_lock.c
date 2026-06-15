// CCCC_FLAGS: --thread-safety
// CCCC_EXPECT_STDERR: DEADLOCK
// Detect a double-lock: a single thread locking a non-recursive mutex twice.
#include <pthread.h>

static pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;

static void *worker(void *arg) {
    (void)arg;
    pthread_mutex_lock(&m);
    pthread_mutex_lock(&m);  // double-lock — should trigger DEADLOCK diagnostic
    pthread_mutex_unlock(&m);
    pthread_mutex_unlock(&m);
    return 0;
}

int main(void) {
    pthread_t t;
    if (pthread_create(&t, 0, worker, 0) != 0)
        return 1;
    pthread_join(t, 0);
    pthread_mutex_destroy(&m);
    return 42;
}
