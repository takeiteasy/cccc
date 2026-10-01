// -c=native/-m/-c=generated OpenMP runtime (#1368): a lazily started thread
// pool, a barrier (macOS has no pthread_barrier), named critical sections,
// worksharing helpers and the omp_* API.
//
// Source of truth for the text tools/gen_shims.py embeds into
// src/shims.inc. NOT COMPILED. Gating lives in serialize_omp_shims() in
// src/serialize_shims.c.

// >>> shim: includes
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
// <<< shim

// >>> shim: runtime
// The pool and barrier are not OpenMP-specific; the kernel runtime reuses them.
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

struct __cccc_team {
    int              n;
    __cccc_barrier_t bar;
    int              single_done;
    void            *copyprivate;
    void           (*fn)(void *);
    void            *env;
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
} __cccc_pool = {PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER,
                 PTHREAD_COND_INITIALIZER, 0, 0, 0, 0};

static int __cccc_omp_requested;

int __cccc_pool_default_threads(void) {
    if (__cccc_omp_requested > 0)
        return __cccc_omp_requested;
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
        seen                = __cccc_pool.gen;
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

void __cccc_omp_barrier(void) {
    if (__cccc_tls_team && __cccc_tls_team->n > 1)
        __cccc_barrier_wait(&__cccc_tls_team->bar);
}

int __cccc_omp_thread_num(void) { return __cccc_tls_tid; }

int __cccc_omp_num_threads(void) {
    return __cccc_tls_team ? __cccc_tls_team->n : 1;
}

// Static schedule: thread t of n gets iterations [*begin, *end) of total.
void __cccc_omp_static_range(long total, long *begin, long *end) {
    long n = __cccc_omp_num_threads(), t = __cccc_omp_thread_num();
    long q = total / n, r = total % n;
    *begin = t * q + (t < r ? t : r);
    *end   = *begin + q + (t < r ? 1 : 0);
}

int __cccc_omp_single_begin(void) {
    struct __cccc_team *t = __cccc_tls_team;
    if (!t || t->n == 1)
        return 1;
    int seq = ++__cccc_tls_single;
    int cur = __atomic_load_n(&t->single_done, __ATOMIC_ACQUIRE);
    while (cur < seq)
        if (__atomic_compare_exchange_n(&t->single_done, &cur, seq, 0,
                                        __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            return 1;
    return 0;
}

static void *__cccc_omp_cp_solo;

void __cccc_omp_copyprivate_set(void *slots) {
    if (__cccc_tls_team)
        __cccc_tls_team->copyprivate = slots;
    else
        __cccc_omp_cp_solo = slots;
}

void *__cccc_omp_copyprivate_get(void) {
    return __cccc_tls_team ? __cccc_tls_team->copyprivate : __cccc_omp_cp_solo;
}

static pthread_mutex_t __cccc_omp_reduce_mutex = PTHREAD_MUTEX_INITIALIZER;

void __cccc_omp_reduce_lock(void) { pthread_mutex_lock(&__cccc_omp_reduce_mutex); }

void __cccc_omp_reduce_unlock(void) {
    pthread_mutex_unlock(&__cccc_omp_reduce_mutex);
}

// TODO: linear name lookup, a hash table if programs use many critical names
// (#1403).
static struct {
    const char     *name;
    pthread_mutex_t m;
} __cccc_omp_crit[64];
static int             __cccc_omp_crit_n;
static pthread_mutex_t __cccc_omp_crit_m = PTHREAD_MUTEX_INITIALIZER;

static pthread_mutex_t *__cccc_omp_crit_find(const char *name) {
    pthread_mutex_lock(&__cccc_omp_crit_m);
    int i = 0;
    while (i < __cccc_omp_crit_n && strcmp(__cccc_omp_crit[i].name, name))
        i++;
    if (i == __cccc_omp_crit_n) {
        if (i == 64)
            __builtin_trap();
        __cccc_omp_crit[i].name = name;
        pthread_mutex_init(&__cccc_omp_crit[i].m, 0);
        __cccc_omp_crit_n++;
    }
    pthread_mutex_unlock(&__cccc_omp_crit_m);
    return &__cccc_omp_crit[i].m;
}

void __cccc_omp_critical_enter(const char *name) {
    pthread_mutex_lock(__cccc_omp_crit_find(name));
}

void __cccc_omp_critical_exit(const char *name) {
    pthread_mutex_unlock(__cccc_omp_crit_find(name));
}

// <<< shim

// >>> shim: lock_core
struct __cccc_omp_lock {
    pthread_mutex_t m;
    unsigned long   owner; // thread holding it, 0 when free
    int             depth;
};

static struct __cccc_omp_lock *__cccc_omp_lock_get(void **slot) {
    return (struct __cccc_omp_lock *)*slot;
}

static void __cccc_omp_lock_init(void **slot) {
    struct __cccc_omp_lock *l = malloc(sizeof(*l));
    pthread_mutex_init(&l->m, 0);
    l->owner = 0;
    l->depth = 0;
    *slot    = l;
}

static void __cccc_omp_lock_destroy(void **slot) {
    struct __cccc_omp_lock *l = __cccc_omp_lock_get(slot);
    if (l) {
        pthread_mutex_destroy(&l->m);
        free(l);
    }
    *slot = 0;
}

static int __cccc_omp_lock_mine(struct __cccc_omp_lock *l) {
    return __atomic_load_n(&l->owner, __ATOMIC_ACQUIRE) ==
           (unsigned long)pthread_self();
}

static void __cccc_omp_lock_take(struct __cccc_omp_lock *l) {
    l->depth = 1;
    __atomic_store_n(&l->owner, (unsigned long)pthread_self(), __ATOMIC_RELEASE);
}

static void __cccc_omp_lock_drop(struct __cccc_omp_lock *l) {
    __atomic_store_n(&l->owner, 0, __ATOMIC_RELEASE);
}

// <<< shim

// >>> shim: locks
// omp_lock_t comes from include/omp.h.
void omp_init_lock(omp_lock_t *lock) { __cccc_omp_lock_init(&lock->__impl); }

void omp_destroy_lock(omp_lock_t *lock) {
    __cccc_omp_lock_destroy(&lock->__impl);
}

void omp_set_lock(omp_lock_t *lock) {
    struct __cccc_omp_lock *l = __cccc_omp_lock_get(&lock->__impl);
    if (__cccc_omp_lock_mine(l)) {
        write(2, "omp_set_lock: lock is already held (deadlock)\n", 46);
        __builtin_trap();
    }
    pthread_mutex_lock(&l->m);
    __cccc_omp_lock_take(l);
}

void omp_unset_lock(omp_lock_t *lock) {
    struct __cccc_omp_lock *l = __cccc_omp_lock_get(&lock->__impl);
    __cccc_omp_lock_drop(l);
    pthread_mutex_unlock(&l->m);
}

int omp_test_lock(omp_lock_t *lock) {
    struct __cccc_omp_lock *l = __cccc_omp_lock_get(&lock->__impl);
    if (pthread_mutex_trylock(&l->m))
        return 0;
    __cccc_omp_lock_take(l);
    return 1;
}

// <<< shim

// >>> shim: nest_locks
// omp_nest_lock_t comes from include/omp.h.
void omp_init_nest_lock(omp_nest_lock_t *lock) {
    __cccc_omp_lock_init(&lock->__impl);
}

void omp_destroy_nest_lock(omp_nest_lock_t *lock) {
    __cccc_omp_lock_destroy(&lock->__impl);
}

void omp_set_nest_lock(omp_nest_lock_t *lock) {
    struct __cccc_omp_lock *l = __cccc_omp_lock_get(&lock->__impl);
    if (__cccc_omp_lock_mine(l)) {
        l->depth++;
        return;
    }
    pthread_mutex_lock(&l->m);
    __cccc_omp_lock_take(l);
}

void omp_unset_nest_lock(omp_nest_lock_t *lock) {
    struct __cccc_omp_lock *l = __cccc_omp_lock_get(&lock->__impl);
    if (--l->depth == 0) {
        __cccc_omp_lock_drop(l);
        pthread_mutex_unlock(&l->m);
    }
}

int omp_test_nest_lock(omp_nest_lock_t *lock) {
    struct __cccc_omp_lock *l = __cccc_omp_lock_get(&lock->__impl);
    if (__cccc_omp_lock_mine(l))
        return ++l->depth;
    if (pthread_mutex_trylock(&l->m))
        return 0;
    __cccc_omp_lock_take(l);
    return 1;
}

// <<< shim

// >>> shim: api
int omp_get_num_threads(void) { return __cccc_omp_num_threads(); }
int omp_get_thread_num(void) { return __cccc_omp_thread_num(); }
int omp_get_max_threads(void) { return __cccc_pool_default_threads(); }
int omp_get_thread_limit(void) { return __cccc_pool_default_threads(); }
int omp_in_parallel(void) { return __cccc_omp_num_threads() > 1; }

int omp_get_num_procs(void) {
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? (int)n : 1;
}

void omp_set_num_threads(int n) { __cccc_omp_requested = n; }
void omp_set_dynamic(int on) { (void)on; }
int  omp_get_dynamic(void) { return 0; }

double omp_get_wtime(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

double omp_get_wtick(void) { return 1e-9; }
// <<< shim
