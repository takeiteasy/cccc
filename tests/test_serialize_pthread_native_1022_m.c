// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: PTHREAD_MUTEX_INITIALIZER
// CCCC_EXPECT_STDOUT: _Thread_local int tls_val
// CCCC_REJECT_STDOUT: \.__handle
// CCCC_REJECT_STDOUT: \.__state
// -m output keeps PTHREAD_MUTEX_INITIALIZER and the thread-local, and exposes
// no pthread internals.
#include <pthread.h>
static pthread_mutex_t mutex   = PTHREAD_MUTEX_INITIALIZER;
_Thread_local int      tls_val = 0;
static void *worker(void *arg) {
    (void)arg;
    tls_val = 7;
    pthread_mutex_lock(&mutex);
    pthread_mutex_unlock(&mutex);
    return (void *)(long)(tls_val == 7 ? 0 : 1);
}
int main(void) {
    tls_val = 42;
    pthread_t t;
    if (pthread_create(&t, 0, worker, 0) != 0)
        return 1;
    void *rc = 0;
    pthread_join(t, &rc);
    if ((long)rc != 0)
        return 2;
    if (tls_val != 42)
        return 3;
    return 42;
}
