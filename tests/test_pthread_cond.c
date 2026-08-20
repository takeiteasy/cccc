// Expected return: 42
#include <pthread.h>

static pthread_mutex_t mutex   = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  cond    = PTHREAD_COND_INITIALIZER;
static int             waiting = 0;
static int             ready   = 0;
static int             result  = 0;

static void *worker(void *arg) {
    (void)arg;
    pthread_mutex_lock(&mutex);
    waiting = 1;
    pthread_cond_signal(&cond);
    while (!ready)
        pthread_cond_wait(&cond, &mutex);
    result = 42;
    pthread_mutex_unlock(&mutex);
    return 0;
}

int main(void) {
    pthread_t thread;
    if (pthread_create(&thread, 0, worker, 0) != 0)
        return 1;

    pthread_mutex_lock(&mutex);
    while (!waiting)
        pthread_cond_wait(&cond, &mutex);
    ready = 1;
    pthread_cond_signal(&cond);
    pthread_mutex_unlock(&mutex);

    if (pthread_join(thread, 0) != 0)
        return 2;
    pthread_cond_destroy(&cond);
    pthread_mutex_destroy(&mutex);
    return result;
}
