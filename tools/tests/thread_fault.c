#include <pthread.h>
static void *worker(void *arg) {
    (void)arg;
    volatile int *p = (volatile int *)0;
    return (void *)(long)*p;
}
int main(void) {
    pthread_t thread;
    if (pthread_create(&thread, 0, worker, 0))
        return 1;
    pthread_join(thread, 0);
    return 42;
}
