// Expected return: 42
// #623: PTHREAD_MUTEX_RECURSIVE / pthread_mutexattr_settype support.
#include <pthread.h>

static pthread_mutex_t mutex;

// Recurses depth times, re-locking the same recursive mutex from the same
// thread each level without an intervening unlock. Would deadlock (or trip
// the double-lock diagnostic) on a non-recursive mutex.
static int recurse(int depth) {
    if (pthread_mutex_lock(&mutex) != 0)
        return -1;
    int result;
    if (depth == 0) {
        result = 0;
    } else {
        result = recurse(depth - 1);
    }
    if (pthread_mutex_unlock(&mutex) != 0)
        return -1;
    return result;
}

int main(void) {
    pthread_mutexattr_t attr;
    if (pthread_mutexattr_init(&attr) != 0)
        return 1;

    int type = -1;
    pthread_mutexattr_gettype(&attr, &type);
    if (type != PTHREAD_MUTEX_DEFAULT)
        return 2;

    if (pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE) != 0)
        return 3;
    pthread_mutexattr_gettype(&attr, &type);
    if (type != PTHREAD_MUTEX_RECURSIVE)
        return 4;

    if (pthread_mutex_init(&mutex, &attr) != 0)
        return 5;
    if (pthread_mutexattr_destroy(&attr) != 0)
        return 6;

    if (recurse(5) != 0)
        return 7;

    // Mutex must be fully unlocked now: a plain trylock from the "same"
    // logical sequence should succeed and be unlockable.
    if (pthread_mutex_trylock(&mutex) != 0)
        return 8;
    if (pthread_mutex_unlock(&mutex) != 0)
        return 9;

    if (pthread_mutex_destroy(&mutex) != 0)
        return 10;

    return 42;
}
