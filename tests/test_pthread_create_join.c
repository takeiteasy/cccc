// Expected return: 42
#include <pthread.h>

static int value = 0;

static void *worker(void *arg) {
    value = *(int *)arg;
    return (void *)1234;
}

int main(void) {
    pthread_t thread;
    int arg = 42;
    void *retval = 0;

    if (pthread_create(&thread, 0, worker, &arg) != 0)
        return 1;
    if (pthread_join(thread, &retval) != 0)
        return 2;
    if (retval != (void *)1234)
        return 3;
    return value;
}
