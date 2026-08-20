// Expected return: 42
#include <pthread.h>

static pthread_mutex_t mutex   = PTHREAD_MUTEX_INITIALIZER;
static int             counter = 0;

static void *worker(void *arg) {
    int n = *(int *)arg;
    int i = 0;
    while (i < n) {
        pthread_mutex_lock(&mutex);
        counter = counter + 1;
        pthread_mutex_unlock(&mutex);
        i = i + 1;
    }
    return 0;
}

int main(void) {
    pthread_t a, b;
    int       n = 21;

    if (pthread_create(&a, 0, worker, &n) != 0)
        return 1;
    if (pthread_create(&b, 0, worker, &n) != 0)
        return 2;
    if (pthread_join(a, 0) != 0)
        return 3;
    if (pthread_join(b, 0) != 0)
        return 4;
    pthread_mutex_destroy(&mutex);
    return counter;
}
