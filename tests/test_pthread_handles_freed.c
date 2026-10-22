// Mutex and condition handles the guest never destroys are released at exit.
#include <pthread.h>
#include <threads.h>

static pthread_mutex_t pm = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  pc = PTHREAD_COND_INITIALIZER;
static mtx_t           cm;
static cnd_t           cc;
static int             ready;

static void *worker(void *arg) {
    (void)arg;
    pthread_mutex_lock(&pm);
    ready = 1;
    pthread_cond_broadcast(&pc);
    pthread_mutex_unlock(&pm);
    mtx_lock(&cm);
    cnd_signal(&cc);
    mtx_unlock(&cm);
    return 0;
}

int main(void) {
    mtx_init(&cm, mtx_plain);
    cnd_init(&cc);
    pthread_t t;
    if (pthread_create(&t, 0, worker, 0) != 0)
        return 1;
    pthread_mutex_lock(&pm);
    while (!ready)
        pthread_cond_wait(&pc, &pm);
    pthread_mutex_unlock(&pm);
    pthread_join(t, 0);
    return 42;
}
