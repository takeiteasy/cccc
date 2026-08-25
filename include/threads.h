#ifndef __THREADS_H
#define __THREADS_H

#ifdef _WIN32
#error "<threads.h> is only available on POSIX targets in CCCC"
#endif

// #1070: angle-bracket for a correct #include_next hand-off under real GCC.
// pthread.h gained its own #include_next hand-off in #1022 (previously
// quoted here, caught by tools/header_resolution_smoke.py's case 7 static
// audit the moment it did) -- errno.h was already angle-bracket for the
// same reason (ETIMEDOUT was otherwise left undeclared). errno.h is NOT on
// is_compiler_owned_header, so unlike the stdint.h/stdarg.h sites elsewhere
// in this batch this is not behaviour-neutral: under --use-system-headers,
// search_include_paths() now prefers system_include_paths first for this
// spelling, same as any other non-owned std header reached by <...> --
// verified against tests/test_use_system_headers_*.c.
#include <pthread.h>
#include "time.h"
#include <errno.h>

/* C11 thread type backed by the existing pthread VM implementation */
typedef pthread_t thrd_t;

/* Thread-specific storage key */
typedef pthread_key_t tss_t;
typedef void (*tss_dtor_t)(void *);

/* C11 7.26.1p7: minimum number of destructor-call passes per thread exit,
   guarding against a destructor that keeps re-setting its own key. */
#define TSS_DTOR_ITERATIONS 4

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

/* call_once: #1088 -- this used to be a macro expanding to a plain
   `if (!*flag) { *flag = 1; func(); }`, safe only because the VM's GIL
   serialises bytecode execution. That expansion happens in CCCC's own
   preprocessor, before either backend (VM cfunc dispatch or -c=native's
   serializer) ever sees it, so a -c=native shim could never intercept it --
   the AST already holds the racy expansion with no record it started life
   as call_once. Under -c=native's real parallelism two threads could both
   observe the flag unset and both run func(). Made a real function instead,
   backed by an atomic CAS on the flag on both backends (VM: wrap_call_once,
   src/stdlib/pthread.c; -c=native: the threads_native_shims entry,
   src/serialize.c) -- so it's race-free everywhere, not just under the GIL.

   #1183: the type itself is named __cccc_once_flag, with `once_flag`
   aliased onto it via #define rather than spelled directly, because glibc's
   own <stdlib.h> (pulled in unconditionally by serialize_threads_shims for
   the call_once shim below) declares its own `once_flag` under any C11/GNU
   dialect (bits/types/once_flag.h, gated on __USE_ISOC11, which a plain
   `-c=native` compile enables by default) -- a real `typedef struct
   __once_flag {...} once_flag;` that collides outright with a same-named
   typedef of a different underlying type ("conflicting types for
   'once_flag'"). This #define is guest-side only: it's expanded away by
   CCCC's own preprocessor before the AST is built, so every guest spelling
   of `once_flag` already resolves to __cccc_once_flag on both backends with
   no user-visible change -- see serialize_threads_shims's own call_once
   shim text (src/serialize_shims.c), which spells the type
   __cccc_once_flag directly for the same reason.

   NOTE: this #define/#undef pair is dropped from -c=native/-m's own
   auto-captured directive replay entirely (line_is_once_flag_alias_directive(),
   src/serialize_program.c) -- do not "fix" a leaked-macro collision here by
   adding a matching #undef right after call_once's prototype below. An
   #undef living in this header affects CCCC's OWN preprocessing too (it
   isn't a native-output-only construct), which un-defines the alias before
   the GUEST's own subsequent `once_flag`/`ONCE_FLAG_INIT` uses ever get
   preprocessed -- confirmed the hard way, see that function's own comment
   for the regression this caused when tried. */
typedef _Atomic int __cccc_once_flag;
#define once_flag      __cccc_once_flag
#define ONCE_FLAG_INIT 0

void call_once(once_flag *flag, void (*func)(void));

/* ---- Thread lifecycle ---- */
int thrd_create(thrd_t *thr, thrd_start_t func, void *arg);
int thrd_join(thrd_t thr, int *res);
void thrd_exit(int res);
int thrd_detach(thrd_t thr);
void thrd_yield(void);
int thrd_sleep(const struct timespec *duration, struct timespec *remaining);
thrd_t thrd_current(void);
int thrd_equal(thrd_t a, thrd_t b);

/* ---- Mutex ---- */
int mtx_init(mtx_t *mtx, int type);
int mtx_lock(mtx_t *mtx);
int mtx_trylock(mtx_t *mtx);
int mtx_timedlock(mtx_t *mtx, const struct timespec *ts);
int mtx_unlock(mtx_t *mtx);
void mtx_destroy(mtx_t *mtx);

/* ---- Condition variable ---- */
int cnd_init(cnd_t *cond);
int cnd_wait(cnd_t *cond, mtx_t *mtx);
int cnd_signal(cnd_t *cond);
int cnd_broadcast(cnd_t *cond);
int cnd_timedwait(cnd_t *cond, mtx_t *mtx, const struct timespec *ts);
void cnd_destroy(cnd_t *cond);

/* ---- Thread-specific storage ---- */
int tss_create(tss_t *key, tss_dtor_t dtor);
void *tss_get(tss_t key);
int tss_set(tss_t key, void *val);
void tss_delete(tss_t key);

#endif
