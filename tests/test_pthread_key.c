// Expected return: 42
#include <pthread.h>

static pthread_key_t key;

static void *worker(void *arg) {
    pthread_setspecific(key, arg);
    return pthread_getspecific(key);
}

int main(void) {
    pthread_t thread;
    int thread_value = 42;
    int main_value = 7;
    void *retval = 0;

    if (pthread_key_create(&key, 0) != 0)
        return 1;
    pthread_setspecific(key, &main_value);
    if (pthread_create(&thread, 0, worker, &thread_value) != 0)
        return 2;
    if (pthread_join(thread, &retval) != 0)
        return 3;
    if (*(int *)pthread_getspecific(key) != 7)
        return 4;
    pthread_key_delete(key);
    return *(int *)retval;
}
