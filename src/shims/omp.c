// -c=native/-m/-c=generated OpenMP runtime (#1368): named critical sections,
// worksharing helpers and the omp_* API, on the pool in pool.c.
//
// Source of truth for the text tools/gen_shims.py embeds into
// src/shims.inc. NOT COMPILED. Gating lives in serialize_threaded_shims() in
// src/serialize_shims.c.

// >>> shim: includes
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
// <<< shim

// >>> shim: runtime
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

void omp_set_num_threads(int n) {
    __cccc_pool_requested = n;
}
void omp_set_dynamic(int on) { (void)on; }
int  omp_get_dynamic(void) { return 0; }

double omp_get_wtime(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

double omp_get_wtick(void) { return 1e-9; }
// <<< shim
