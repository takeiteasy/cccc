#ifndef __CCCC_PTHREAD_H
#define __CCCC_PTHREAD_H

#ifdef _WIN32
#error "<pthread.h> is only available on POSIX targets in CCCC"
#endif

/* #1022: this exact file is also what a native/generated re-emission's */
/* replayed `#include <pthread.h>` resolves to (run_native_backend forwards */
/* -I./include straight through to the host cc, and -I paths are searched */
/* ahead of system directories) -- but the VM-ABI struct layouts below */
/* (pthread_mutex_t/pthread_cond_t/... as a plain {void*,long,int} triple) */
/* are CCCC's own polyfill for the FFI wrappers in src/stdlib/pthread.c, not */
/* the real host layout (64/48 bytes on macOS arm64 vs. the 24/16 bytes */
/* below) -- a real host compiler reading this file's projection and then */
/* linking against the real libpthread silently corrupts every pthread */
/* object it touches (confirmed: test_pthread_mutex.c's -c=native binary */
/* flaked non-42 exit codes under repeated runs before this fix). Same */
/* shape as #1021/#1040 (fenv.h/stdio.h) -- guard the whole CCCC-flavored */
/* body and hand off to the host's own <pthread.h> via #include_next. */
/* __CCCC__ is defined unconditionally by CCCC's own preprocessor before any */
/* header is read, so its absence here means a genuine host compiler is */
/* reprocessing this file -- only possible during -c=native/-c=generated */
/* serializer replay. */
#ifdef __CCCC__

#include "stddef.h"
#include "time.h"

typedef void        *pthread_t;
typedef unsigned int pthread_key_t;

typedef struct {
    void *__handle;
    long  __state;
    int   __type;
} pthread_mutex_t;

typedef struct {
    void *__handle;
    long  __state;
} pthread_cond_t;

typedef struct {
    size_t __stack_size;
    void  *__stack_addr;
} pthread_attr_t;

typedef struct {
    int __type;
} pthread_mutexattr_t;

typedef struct {
    int __unused;
} pthread_condattr_t;

#define PTHREAD_MUTEX_INITIALIZER {0, 0, 0}
#define PTHREAD_COND_INITIALIZER  {0, 0}

/* Mutex types (POSIX). Values are the real host encoding so they can be */
/* forwarded directly to the native pthread_mutexattr_settype() underneath */
/* the FFI wrapper -- see src/stdlib/pthread.c. */
#ifdef __APPLE__
#define PTHREAD_MUTEX_NORMAL     0
#define PTHREAD_MUTEX_ERRORCHECK 1
#define PTHREAD_MUTEX_RECURSIVE  2
#define PTHREAD_MUTEX_DEFAULT    PTHREAD_MUTEX_NORMAL
#else
#define PTHREAD_MUTEX_NORMAL     0
#define PTHREAD_MUTEX_RECURSIVE  1
#define PTHREAD_MUTEX_ERRORCHECK 2
#define PTHREAD_MUTEX_DEFAULT    PTHREAD_MUTEX_NORMAL
#endif

int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                   void *(*start_routine)(void *), void    *arg);
int pthread_join(pthread_t thread, void **retval);
int pthread_detach(pthread_t thread);
void pthread_exit(void *retval);
pthread_t pthread_self(void);
int pthread_equal(pthread_t t1, pthread_t t2);

int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr);
int pthread_mutex_destroy(pthread_mutex_t *mutex);
int pthread_mutex_lock(pthread_mutex_t *mutex);
int pthread_mutex_trylock(pthread_mutex_t *mutex);
int pthread_mutex_unlock(pthread_mutex_t *mutex);

int pthread_mutexattr_init(pthread_mutexattr_t *attr);
int pthread_mutexattr_destroy(pthread_mutexattr_t *attr);
int pthread_mutexattr_settype(pthread_mutexattr_t *attr, int type);
int pthread_mutexattr_gettype(const pthread_mutexattr_t *attr, int *type);

int pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr);
int pthread_cond_destroy(pthread_cond_t *cond);
int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex);
int pthread_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex,
                           const struct timespec *abstime);
int pthread_cond_signal(pthread_cond_t *cond);
int pthread_cond_broadcast(pthread_cond_t *cond);

int pthread_key_create(pthread_key_t *key, void (*destructor)(void *));
int pthread_key_delete(pthread_key_t key);
void *pthread_getspecific(pthread_key_t key);
int pthread_setspecific(pthread_key_t key, const void *value);

int pthread_attr_init(pthread_attr_t *attr);
int pthread_attr_destroy(pthread_attr_t *attr);
int pthread_attr_setstacksize(pthread_attr_t *attr, size_t stacksize);
int pthread_attr_getstack(const pthread_attr_t *attr, void **stackaddr,
                          size_t *stacksize);

#else
#include_next <pthread.h>
#endif /* __CCCC__ */

#endif /* __CCCC_PTHREAD_H */
