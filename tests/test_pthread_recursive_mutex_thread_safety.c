// CCCC_FLAGS: --thread-safety
// Expected return: 42
// #623: PTHREAD_MUTEX_RECURSIVE must bypass the --thread-safety double-lock
// deadlock diagnostic (wrap_pthread_mutex_lock's mutex->type !=
// PTHREAD_MUTEX_RECURSIVE guard in src/stdlib/pthread.c). Without the guard,
// this same-thread re-lock would print the "DEADLOCK: double-lock detected"
// diagnostic and return EDEADLK instead of succeeding.
#include <pthread.h>

static pthread_mutex_t mutex;

int main(void) {
    pthread_mutexattr_t attr;
    if (pthread_mutexattr_init(&attr) != 0)
        return 1;
    if (pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE) != 0)
        return 2;
    if (pthread_mutex_init(&mutex, &attr) != 0)
        return 3;
    pthread_mutexattr_destroy(&attr);

    if (pthread_mutex_lock(&mutex) != 0)
        return 4;
    // Re-lock from the same thread without an intervening unlock. Under
    // --thread-safety this must succeed (recursive), not EDEADLK.
    if (pthread_mutex_lock(&mutex) != 0)
        return 5;

    if (pthread_mutex_unlock(&mutex) != 0)
        return 6;
    if (pthread_mutex_unlock(&mutex) != 0)
        return 7;

    pthread_mutex_destroy(&mutex);
    return 42;
}
