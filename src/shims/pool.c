// Thread pool and barrier shared by the OpenMP runtime (omp.c) and the kernel
// runtime (kernel.c), emitted once when either is used (#1368, #1394).
//
// Source of truth for the text tools/gen_shims.py embeds into
// src/shims.inc. NOT COMPILED. Gating lives in serialize_threaded_shims() in
// src/serialize_shims.c.

// >>> shim: includes
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
// <<< shim

// >>> shim: runtime
// OMP_NUM_THREADS also sizes the pool a kernel launch runs on.
typedef struct {
    pthread_mutex_t m;
    pthread_cond_t  c;
    int             n, waiting;
    unsigned        gen;
} __cccc_barrier_t;

static void __cccc_barrier_init(__cccc_barrier_t *b, int n) {
    pthread_mutex_init(&b->m, 0);
    pthread_cond_init(&b->c, 0);
    b->n       = n;
    b->waiting = 0;
    b->gen     = 0;
}

static void __cccc_barrier_destroy(__cccc_barrier_t *b) {
    pthread_mutex_destroy(&b->m);
    pthread_cond_destroy(&b->c);
}

static void __cccc_barrier_wait(__cccc_barrier_t *b) {
    pthread_mutex_lock(&b->m);
    unsigned gen = b->gen;
    if (++b->waiting == b->n) {
        b->waiting = 0;
        b->gen++;
        pthread_cond_broadcast(&b->c);
    } else {
        while (gen == b->gen)
            pthread_cond_wait(&b->c, &b->m);
    }
    pthread_mutex_unlock(&b->m);
}

// A participant that will never reach the barrier again: the rest stop waiting
// for it.
static void __cccc_barrier_leave(__cccc_barrier_t *b) {
    pthread_mutex_lock(&b->m);
    b->n--;
    if (b->n > 0 && b->waiting == b->n) {
        b->waiting = 0;
        b->gen++;
        pthread_cond_broadcast(&b->c);
    }
    pthread_mutex_unlock(&b->m);
}

struct __cccc_team {
    int              n;
    __cccc_barrier_t bar;
    int              single_done;
    void            *copyprivate;
    void (*fn)(void *);
    void *env;
};

static __thread struct __cccc_team *__cccc_tls_team;
static __thread int                 __cccc_tls_tid;
static __thread int                 __cccc_tls_single;

static struct {
    pthread_mutex_t     m;
    pthread_cond_t      wake, done;
    int                 started, pending;
    unsigned            gen;
    struct __cccc_team *team;
} __cccc_pool = {PTHREAD_MUTEX_INITIALIZER,
                 PTHREAD_COND_INITIALIZER,
                 PTHREAD_COND_INITIALIZER,
                 0,
                 0,
                 0,
                 0};

static int __cccc_pool_requested;

int __cccc_pool_default_threads(void) {
    if (__cccc_pool_requested > 0)
        return __cccc_pool_requested;
    const char *env = getenv("OMP_NUM_THREADS");
    if (env && atoi(env) > 0)
        return atoi(env);
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? (int)n : 1;
}

static void __cccc_team_run(struct __cccc_team *t, int tid) {
    struct __cccc_team *saved_team   = __cccc_tls_team;
    int                 saved_tid    = __cccc_tls_tid;
    int                 saved_single = __cccc_tls_single;
    __cccc_tls_team                  = t;
    __cccc_tls_tid                   = tid;
    __cccc_tls_single                = 0;
    t->fn(t->env);
    __cccc_tls_team   = saved_team;
    __cccc_tls_tid    = saved_tid;
    __cccc_tls_single = saved_single;
}

static void *__cccc_pool_worker(void *arg) {
    int      id   = (int)(long)arg;
    unsigned seen = 0;
    for (;;) {
        pthread_mutex_lock(&__cccc_pool.m);
        while (__cccc_pool.gen == seen)
            pthread_cond_wait(&__cccc_pool.wake, &__cccc_pool.m);
        seen                  = __cccc_pool.gen;
        struct __cccc_team *t = __cccc_pool.team;
        pthread_mutex_unlock(&__cccc_pool.m);
        if (id < t->n) {
            __cccc_team_run(t, id);
            pthread_mutex_lock(&__cccc_pool.m);
            if (--__cccc_pool.pending == 0)
                pthread_cond_signal(&__cccc_pool.done);
            pthread_mutex_unlock(&__cccc_pool.m);
        }
    }
    return 0;
}

// Runs fn(env) on a team of n threads (0 = default) and returns when every
// thread has finished. A region started inside a team runs as a team of one.
void __cccc_pool_run(void (*fn)(void *), void *env, int n) {
    if (n <= 0)
        n = __cccc_pool_default_threads();
    if (__cccc_tls_team)
        n = 1;
    struct __cccc_team t;
    t.n           = n;
    t.single_done = 0;
    t.copyprivate = 0;
    t.fn          = fn;
    t.env         = env;
    __cccc_barrier_init(&t.bar, n);
    if (n > 1) {
        pthread_mutex_lock(&__cccc_pool.m);
        // Workers are detached and exit with the process.
        while (__cccc_pool.started < n - 1) {
            pthread_t th;
            pthread_create(&th, 0, __cccc_pool_worker,
                           (void *)(long)(__cccc_pool.started + 1));
            pthread_detach(th);
            __cccc_pool.started++;
        }
        __cccc_pool.team    = &t;
        __cccc_pool.pending = n - 1;
        __cccc_pool.gen++;
        pthread_cond_broadcast(&__cccc_pool.wake);
        pthread_mutex_unlock(&__cccc_pool.m);
    }
    __cccc_team_run(&t, 0);
    if (n > 1) {
        pthread_mutex_lock(&__cccc_pool.m);
        while (__cccc_pool.pending)
            pthread_cond_wait(&__cccc_pool.done, &__cccc_pool.m);
        pthread_mutex_unlock(&__cccc_pool.m);
    }
    __cccc_barrier_destroy(&t.bar);
}
// <<< shim
