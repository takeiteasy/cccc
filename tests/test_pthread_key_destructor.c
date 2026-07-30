// Expected return: 42
// C11 7.26.6.1p2 / POSIX pthread_key_create: a non-NULL key value must have
// its destructor invoked when the owning thread exits. Also checks the
// negative case: a key deleted with pthread_key_delete() must NOT have its
// destructor invoked on a later thread's exit.
#include <pthread.h>
#include <stdlib.h>

static pthread_key_t g_key;
static pthread_key_t g_deleted_key;
static int g_dtor_calls = 0;
static int g_dtor_last_value = -1;
static int g_deleted_dtor_calls = 0;

static void dtor(void *p) {
    g_dtor_calls++;
    g_dtor_last_value = *(int *)p;
    free(p);
}

static void deleted_dtor(void *p) {
    g_deleted_dtor_calls++;
    free(p);
}

static void *worker(void *arg) {
    int *slot = malloc(sizeof(int));
    *slot = *(int *)arg;
    pthread_setspecific(g_key, slot);

    // g_deleted_key was already pthread_key_delete()'d, so setspecific must
    // reject it -- nothing is stored, so nothing for this thread to own.
    int deleted_slot = 0;
    if (pthread_setspecific(g_deleted_key, &deleted_slot) == 0)
        exit(8);

    return NULL;
}

int main(void) {
    if (pthread_key_create(&g_key, dtor) != 0)
        return 1;
    if (pthread_key_create(&g_deleted_key, deleted_dtor) != 0)
        return 2;
    // Deleting the key before the worker sets a value must suppress its
    // destructor entirely, matching C11/POSIX semantics.
    pthread_key_delete(g_deleted_key);

    pthread_t thread;
    int val = 123;
    if (pthread_create(&thread, 0, worker, &val) != 0)
        return 3;
    if (pthread_join(thread, NULL) != 0)
        return 4;

    if (g_dtor_calls != 1)          return 5;
    if (g_dtor_last_value != 123)   return 6;
    if (g_deleted_dtor_calls != 0)  return 7;

    pthread_key_delete(g_key);
    return 42;
}
