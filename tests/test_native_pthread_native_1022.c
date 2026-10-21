// pthread types and a thread-local round-trip under -c=native.
// CCCC_LEAKS_SKIP: guest pthread handles are never released (#1730)
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
