#ifndef __THREADS_H
#define __THREADS_H

#ifdef _WIN32
#error "<threads.h> is only available on POSIX targets in CCCC"
#endif

#include "pthread.h"
#include "time.h"
#include "errno.h"

/* C11 thread type backed by the existing pthread VM implementation */
typedef pthread_t thrd_t;

/* Thread-specific storage key */
typedef pthread_key_t tss_t;
typedef void (*tss_dtor_t)(void *);

/* Mutex: store the C11 type flag so mtx_init can configure appropriately.
   Layout mirrors CCCCUserMutex (void *__handle, long __state) plus an int
   type tag; the host mutex is lazily allocated on first lock. */
typedef struct {
    void *__handle;
    long  __state;
    int   __type;
} mtx_t;

/* Condition variable backed by pthread cond */
typedef struct {
    void *__handle;
    long  __state;
} cnd_t;

/* Thread start function returning int (differs from pthread's void*) */
typedef int (*thrd_start_t)(void *);

/* Mutex type flags */
enum {
    mtx_plain     = 0,
    mtx_recursive = 1,
    mtx_timed     = 2,
};

/* Return codes */
enum {
    thrd_success  = 0,
    thrd_error    = 1,
    thrd_timedout = ETIMEDOUT,
    thrd_busy     = EBUSY,
    thrd_nomem    = ENOMEM,
};

/* call_once: under the GIL, bytecode execution is serialised so a simple
   flag check + call is atomic from the VM's perspective. */
typedef _Atomic int once_flag;
#define ONCE_FLAG_INIT 0

#define call_once(flag, func) \
    do { if (!(*(flag))) { *(flag) = 1; (func)(); } } while (0)

/* ---- Thread lifecycle ---- */
int    thrd_create(thrd_t *thr, thrd_start_t func, void *arg);
int    thrd_join(thrd_t thr, int *res);
void   thrd_exit(int res);
int    thrd_detach(thrd_t thr);
void   thrd_yield(void);
int    thrd_sleep(const struct timespec *duration, struct timespec *remaining);
thrd_t thrd_current(void);
int    thrd_equal(thrd_t a, thrd_t b);

/* ---- Mutex ---- */
int  mtx_init(mtx_t *mtx, int type);
int  mtx_lock(mtx_t *mtx);
int  mtx_trylock(mtx_t *mtx);
int  mtx_timedlock(mtx_t *mtx, const struct timespec *ts);
int  mtx_unlock(mtx_t *mtx);
void mtx_destroy(mtx_t *mtx);

/* ---- Condition variable ---- */
int  cnd_init(cnd_t *cond);
int  cnd_wait(cnd_t *cond, mtx_t *mtx);
int  cnd_signal(cnd_t *cond);
int  cnd_broadcast(cnd_t *cond);
int  cnd_timedwait(cnd_t *cond, mtx_t *mtx, const struct timespec *ts);
void cnd_destroy(cnd_t *cond);

/* ---- Thread-specific storage ---- */
int   tss_create(tss_t *key, tss_dtor_t dtor);
void *tss_get(tss_t key);
int   tss_set(tss_t key, void *val);
void  tss_delete(tss_t key);

#endif
