#include <stdio.h>
#include <pthread.h>

static int counter = 0;

void *thread_fn(void *arg) {
    int id = *(int*)arg;
    printf("Thread %d running\n", id);
    counter++;
    return NULL;
}

int main(void) {
    pthread_t t1, t2;
    int id1 = 1, id2 = 2;

    pthread_create(&t1, NULL, thread_fn, &id1);
    pthread_create(&t2, NULL, thread_fn, &id2);

    pthread_join(t1, NULL);
    pthread_join(t2, NULL);

    printf("Counter: %d\n", counter);
    return 0;
}
