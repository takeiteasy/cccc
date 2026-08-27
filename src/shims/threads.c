// -c=native C11 <threads.h> shims (#1088): thrd_*/mtx_*/cnd_*/tss_*/
// call_once, ported from src/stdlib/pthread.c minus the GIL bookkeeping.
//
// Source of truth for the text tools/gen_shims.py embeds into
// src/shims.inc. NOT COMPILED. Gating and rationale live in the
// matching serialize_*_shims() in src/serialize_shims.c.

// >>> shim: includes
#include <pthread.h>
#include <time.h>
#include <errno.h>
#include <stdlib.h>
// <<< shim

// >>> shim: thrd_trampoline
_Static_assert(sizeof(pthread_t) <= sizeof(void *),
               "cccc: host pthread_t must fit in a pointer-sized thrd_t");
struct __cccc_thrd_args { int (*fn)(void *); void *arg; };
static void *__cccc_thrd_trampoline(void *argp) {
    struct __cccc_thrd_args *a = (struct __cccc_thrd_args *)argp;
    int rc = a->fn(a->arg);
    free(a);
    return (void *)(long)rc;
}
// <<< shim

// >>> shim: ensure_mtx
static pthread_mutex_t *__cccc_ensure_mtx(mtx_t *mtx) {
    if (!mtx) return NULL;
    void *h = __atomic_load_n(&mtx->__handle, __ATOMIC_ACQUIRE);
    if (h) return (pthread_mutex_t *)h;
    pthread_mutex_t *host = malloc(sizeof(*host));
    if (!host) return NULL;
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    if (mtx->__type == 1)
        pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    if (pthread_mutex_init(host, &attr) != 0) {
        pthread_mutexattr_destroy(&attr);
        free(host);
        return NULL;
    }
    pthread_mutexattr_destroy(&attr);
    void *expected = NULL;
    if (!__atomic_compare_exchange_n(&mtx->__handle, &expected, host, 0,
                                      __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        pthread_mutex_destroy(host);
        free(host);
        return (pthread_mutex_t *)expected;
    }
    mtx->__state = 1;
    return host;
}
// <<< shim

// >>> shim: ensure_cnd
static pthread_cond_t *__cccc_ensure_cnd(cnd_t *cond) {
    if (!cond) return NULL;
    void *h = __atomic_load_n(&cond->__handle, __ATOMIC_ACQUIRE);
    if (h) return (pthread_cond_t *)h;
    pthread_cond_t *host = malloc(sizeof(*host));
    if (!host) return NULL;
    if (pthread_cond_init(host, NULL) != 0) {
        free(host);
        return NULL;
    }
    void *expected = NULL;
    if (!__atomic_compare_exchange_n(&cond->__handle, &expected, host, 0,
                                      __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        pthread_cond_destroy(host);
        free(host);
        return (pthread_cond_t *)expected;
    }
    cond->__state = 1;
    return host;
}
// <<< shim

// >>> shim: thrd_create
int thrd_create(thrd_t *thr, thrd_start_t func, void *arg) {
    struct __cccc_thrd_args *a = malloc(sizeof(*a));
    if (!a) return ENOMEM;
    a->fn = func;
    a->arg = arg;
    pthread_t host;
    int rc = pthread_create(&host, NULL, __cccc_thrd_trampoline, a);
    if (rc != 0) {
        free(a);
        return rc == ENOMEM ? ENOMEM : 1;
    }
    thrd_t out = 0;
    __builtin_memcpy(&out, &host, sizeof(host));
    *thr = out;
    return 0;
}
// <<< shim

// >>> shim: thrd_join
int thrd_join(thrd_t thr, int *res) {
    pthread_t host;
    __builtin_memcpy(&host, &thr, sizeof(host));
    void *retval = NULL;
    if (pthread_join(host, &retval) != 0) return 1;
    if (res) *res = (int)(long)retval;
    return 0;
}
// <<< shim

// >>> shim: thrd_exit
_Noreturn void thrd_exit(int res) {
    pthread_exit((void *)(long)res);
}
// <<< shim

// >>> shim: thrd_detach
int thrd_detach(thrd_t thr) {
    pthread_t host;
    __builtin_memcpy(&host, &thr, sizeof(host));
    return pthread_detach(host) == 0 ? 0 : 1;
}
// <<< shim

// >>> shim: thrd_yield
void thrd_yield(void) { sched_yield(); }
// <<< shim

// >>> shim: thrd_sleep
int thrd_sleep(const struct timespec *duration, struct timespec *remaining) {
    if (!duration) return -2;
    int rc = nanosleep(duration, remaining);
    if (rc == 0) return 0;
    return errno == EINTR ? -1 : -2;
}
// <<< shim

// >>> shim: thrd_current
thrd_t thrd_current(void) {
    pthread_t self = pthread_self();
    thrd_t out = 0;
    __builtin_memcpy(&out, &self, sizeof(self));
    return out;
}
// <<< shim

// >>> shim: thrd_equal
int thrd_equal(thrd_t a, thrd_t b) {
    pthread_t pa, pb;
    __builtin_memcpy(&pa, &a, sizeof(pa));
    __builtin_memcpy(&pb, &b, sizeof(pb));
    return pthread_equal(pa, pb) != 0;
}
// <<< shim

// >>> shim: mtx_init
int mtx_init(mtx_t *mtx, int type) {
    if (!mtx) return 1;
    if (__atomic_load_n(&mtx->__handle, __ATOMIC_ACQUIRE))
        return 1;
    mtx->__type = type;
    return __cccc_ensure_mtx(mtx) ? 0 : 1;
}
// <<< shim

// >>> shim: mtx_lock
int mtx_lock(mtx_t *mtx) {
    pthread_mutex_t *host = __cccc_ensure_mtx(mtx);
    if (!host) return 1;
    return pthread_mutex_lock(host) == 0 ? 0 : 1;
}
// <<< shim

// >>> shim: mtx_trylock
int mtx_trylock(mtx_t *mtx) {
    pthread_mutex_t *host = __cccc_ensure_mtx(mtx);
    if (!host) return 1;
    int rc = pthread_mutex_trylock(host);
    if (rc == 0) return 0;
    return rc == EBUSY ? EBUSY : 1;
}
// <<< shim

// >>> shim: mtx_timedlock
int mtx_timedlock(mtx_t *mtx, const struct timespec *ts) {
    pthread_mutex_t *host = __cccc_ensure_mtx(mtx);
    if (!host || !ts) return 1;
    int rc;
#if defined(__linux__)
    rc = pthread_mutex_timedlock(host, ts);
#else
    for (;;) {
        rc = pthread_mutex_trylock(host);
        if (rc == 0) break;
        struct timespec now;
        clock_gettime(0 /* CLOCK_REALTIME */, &now);
        if (now.tv_sec > ts->tv_sec ||
            (now.tv_sec == ts->tv_sec && now.tv_nsec >= ts->tv_nsec)) {
            rc = ETIMEDOUT;
            break;
        }
        struct timespec delay = {0, 1000000};
        nanosleep(&delay, NULL);
    }
#endif
    if (rc == 0) return 0;
    return rc == ETIMEDOUT ? ETIMEDOUT : 1;
}
// <<< shim

// >>> shim: mtx_unlock
int mtx_unlock(mtx_t *mtx) {
    if (!mtx || !mtx->__handle) return 1;
    return pthread_mutex_unlock((pthread_mutex_t *)mtx->__handle) == 0 ? 0 : 1;
}
// <<< shim

// >>> shim: mtx_destroy
void mtx_destroy(mtx_t *mtx) {
    if (!mtx || !mtx->__handle) return;
    pthread_mutex_destroy((pthread_mutex_t *)mtx->__handle);
    free(mtx->__handle);
    mtx->__handle = NULL;
    mtx->__state = 0;
}
// <<< shim

// >>> shim: cnd_init
int cnd_init(cnd_t *cond) {
    if (!cond) return 1;
    return __cccc_ensure_cnd(cond) ? 0 : 1;
}
// <<< shim

// >>> shim: cnd_wait
int cnd_wait(cnd_t *cond, mtx_t *mtx) {
    pthread_cond_t *c = __cccc_ensure_cnd(cond);
    pthread_mutex_t *m = __cccc_ensure_mtx(mtx);
    if (!c || !m) return 1;
    return pthread_cond_wait(c, m) == 0 ? 0 : 1;
}
// <<< shim

// >>> shim: cnd_signal
int cnd_signal(cnd_t *cond) {
    pthread_cond_t *c = __cccc_ensure_cnd(cond);
    if (!c) return 1;
    return pthread_cond_signal(c) == 0 ? 0 : 1;
}
// <<< shim

// >>> shim: cnd_broadcast
int cnd_broadcast(cnd_t *cond) {
    pthread_cond_t *c = __cccc_ensure_cnd(cond);
    if (!c) return 1;
    return pthread_cond_broadcast(c) == 0 ? 0 : 1;
}
// <<< shim

// >>> shim: cnd_timedwait
int cnd_timedwait(cnd_t *cond, mtx_t *mtx, const struct timespec *ts) {
    pthread_cond_t *c = __cccc_ensure_cnd(cond);
    pthread_mutex_t *m = __cccc_ensure_mtx(mtx);
    if (!c || !m || !ts) return 1;
    int rc = pthread_cond_timedwait(c, m, ts);
    if (rc == 0) return 0;
    return rc == ETIMEDOUT ? ETIMEDOUT : 1;
}
// <<< shim

// >>> shim: cnd_destroy
void cnd_destroy(cnd_t *cond) {
    if (!cond || !cond->__handle) return;
    pthread_cond_destroy((pthread_cond_t *)cond->__handle);
    free(cond->__handle);
    cond->__handle = NULL;
    cond->__state = 0;
}
// <<< shim

// >>> shim: tss_create
int tss_create(tss_t *key, tss_dtor_t dtor) {
    return pthread_key_create(key, dtor) == 0 ? 0 : 1;
}
// <<< shim

// >>> shim: tss_get
void *tss_get(tss_t key) { return pthread_getspecific(key); }
// <<< shim

// >>> shim: tss_set
int tss_set(tss_t key, void *val) {
    return pthread_setspecific(key, val) == 0 ? 0 : 1;
}
// <<< shim

// >>> shim: tss_delete
void tss_delete(tss_t key) { pthread_key_delete(key); }
// <<< shim

// >>> shim: call_once
void __cccc_call_once(__cccc_once_flag *flag, void (*func)(void)) {
    int *raw = (int *)flag;
    int expected = 0;
    if (__atomic_compare_exchange_n(raw, &expected, 1, 0,
                                     __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        func();
        __atomic_store_n(raw, 2, __ATOMIC_RELEASE);
    } else {
        while (__atomic_load_n(raw, __ATOMIC_ACQUIRE) != 2)
            sched_yield();
    }
}
// <<< shim
