// CCCC_FLAGS: --thread-safety
// CCCC_EXPECT_STDERR: LOCK ORDER
// Detect a lock-order inversion: thread A acquires L1 then L2, thread B
// acquires L2 then L1 — the reverse order is a potential deadlock.
#include <pthread.h>

static pthread_mutex_t l1 = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t l2 = PTHREAD_MUTEX_INITIALIZER;

static void *thread_a(void *arg) {
    (void)arg;
    pthread_mutex_lock(&l1);
    pthread_mutex_lock(&l2);
    pthread_mutex_unlock(&l2);
    pthread_mutex_unlock(&l1);
    return 0;
}

static void *thread_b(void *arg) {
    (void)arg;
    pthread_mutex_lock(&l2);
    pthread_mutex_lock(&l1);  // reverse order — triggers LOCK ORDER INVERSION
    pthread_mutex_unlock(&l1);
    pthread_mutex_unlock(&l2);
    return 0;
}

int main(void) {
    pthread_t ta, tb;
    if (pthread_create(&ta, 0, thread_a, 0) != 0)
        return 1;
    pthread_join(ta, 0);
    if (pthread_create(&tb, 0, thread_b, 0) != 0)
        return 2;
    pthread_join(tb, 0);
    pthread_mutex_destroy(&l1);
    pthread_mutex_destroy(&l2);
    return 42;
}
