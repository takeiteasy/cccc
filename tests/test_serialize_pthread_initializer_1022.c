// Expected return: 42
// #1022: PTHREAD_MUTEX_INITIALIZER/PTHREAD_COND_INITIALIZER serialized as
// CCCC's own projected designated-initializer image (e.g.
// `{ .__handle = 0, .__state = 0, .__type = 0 }`) instead of the bare host
// macro -- a hard compile error under -c=native once include/pthread.h
// hands off to the real host <pthread.h> (its real pthread_mutex_t/
// pthread_cond_t have no such members). Confirmed failing pre-fix.
#include <pthread.h>

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  cond  = PTHREAD_COND_INITIALIZER;
static int             ready = 0;

static void *worker(void *arg) {
    (void)arg;
    pthread_mutex_lock(&mutex);
    ready = 1;
    pthread_cond_signal(&cond);
    pthread_mutex_unlock(&mutex);
    return 0;
}

int main(void) {
    pthread_t t;
    if (pthread_create(&t, 0, worker, 0) != 0)
        return 1;

    pthread_mutex_lock(&mutex);
    while (!ready)
        pthread_cond_wait(&cond, &mutex);
    pthread_mutex_unlock(&mutex);

    pthread_join(t, 0);

    if (pthread_mutex_lock(&mutex) != 0)
        return 2;
    if (pthread_mutex_unlock(&mutex) != 0)
        return 3;
    if (pthread_mutex_destroy(&mutex) != 0)
        return 4;
    if (pthread_cond_destroy(&cond) != 0)
        return 5;

    return 42;
}
