/* Manual pthread declarations to bypass header parsing issues */
typedef unsigned long pthread_t;
typedef struct { long __opaque[8]; } pthread_mutex_t;
typedef void *pthread_attr_t;

int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                   void *(*start_routine)(void *), void *arg);
int pthread_join(pthread_t thread, void **retval);
int pthread_mutex_init(pthread_mutex_t *mutex, const void *attr);
int pthread_mutex_lock(pthread_mutex_t *mutex);
int pthread_mutex_unlock(pthread_mutex_t *mutex);
int pthread_mutex_destroy(pthread_mutex_t *mutex);

#include <stdio.h>

static int counter = 0;

void *thread_fn(void *arg) {
    int id = *(int*)arg;
    printf("Thread %d running\n", id);
    counter++;
    return 0;
}

int main(void) {
    pthread_t t1, t2;
    int id1 = 1, id2 = 2;

    pthread_create(&t1, 0, thread_fn, &id1);
    pthread_create(&t2, 0, thread_fn, &id2);

    pthread_join(t1, 0);
    pthread_join(t2, 0);

    printf("Counter: %d\n", counter);
    return 0;
}
