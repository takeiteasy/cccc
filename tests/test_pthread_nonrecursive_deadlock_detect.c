// CCCC_FLAGS: --thread-safety
// Expected return: 42
// #623 regression guard: the recursive-mutex bypass added to
// wrap_pthread_mutex_lock (src/stdlib/pthread.c) must only skip the
// --thread-safety double-lock diagnostic for PTHREAD_MUTEX_RECURSIVE
// mutexes. A plain (non-recursive, default-type) mutex re-locked by the
// same thread must still be caught and return EDEADLK.
#include <errno.h>
#include <pthread.h>

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

int main(void) {
    if (pthread_mutex_lock(&mutex) != 0)
        return 1;
    int rc = pthread_mutex_lock(&mutex);
    if (rc != EDEADLK)
        return 2;
    if (pthread_mutex_unlock(&mutex) != 0)
        return 3;
    pthread_mutex_destroy(&mutex);
    return 42;
}
