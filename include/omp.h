#ifndef __OMP_H
#define __OMP_H

/* OpenMP runtime for -fopenmp. Parallel regions run on one thread, so the
   team is always the initial thread and locks cannot contend. */

#include <stdio.h>
#include <time.h>
#include <unistd.h>

#ifdef __CCCC_OMP_THREADED__

/* -c=native, -m and -c=generated run parallel regions on real threads; the
   runtime is emitted into the output. */

typedef struct {
    void *__impl;
} omp_lock_t;

typedef struct {
    void *__impl;
    int   __reserved; /* keeps it a distinct type from omp_lock_t */
} omp_nest_lock_t;

int    omp_get_num_threads(void);
int    omp_get_max_threads(void);
int    omp_get_thread_num(void);
int    omp_get_thread_limit(void);
int    omp_in_parallel(void);
int    omp_get_num_procs(void);
void   omp_set_num_threads(int n);
void   omp_set_dynamic(int on);
int    omp_get_dynamic(void);
double omp_get_wtime(void);
double omp_get_wtick(void);

void omp_init_lock(omp_lock_t *lock);
void omp_destroy_lock(omp_lock_t *lock);
void omp_set_lock(omp_lock_t *lock);
void omp_unset_lock(omp_lock_t *lock);
int  omp_test_lock(omp_lock_t *lock);

void omp_init_nest_lock(omp_nest_lock_t *lock);
void omp_destroy_nest_lock(omp_nest_lock_t *lock);
void omp_set_nest_lock(omp_nest_lock_t *lock);
void omp_unset_nest_lock(omp_nest_lock_t *lock);
int  omp_test_nest_lock(omp_nest_lock_t *lock);

#else

typedef struct {
    int held;
} omp_lock_t;

typedef struct {
    int depth;
} omp_nest_lock_t;

static inline int omp_get_num_threads(void) { return 1; }
static inline int omp_get_max_threads(void) { return 1; }
static inline int omp_get_thread_num(void) { return 0; }
static inline int omp_get_thread_limit(void) { return 1; }
static inline int omp_in_parallel(void) { return 0; }
static inline int omp_get_num_procs(void) {
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? (int)n : 1;
}
static inline void omp_set_num_threads(int n) { (void)n; }
static inline void omp_set_dynamic(int on) { (void)on; }
static inline int omp_get_dynamic(void) { return 0; }

static inline double omp_get_wtime(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}
static inline double omp_get_wtick(void) { return 1e-9; }

static inline void omp_init_lock(omp_lock_t *lock) { lock->held = 0; }
static inline void omp_destroy_lock(omp_lock_t *lock) { lock->held = 0; }
static inline void omp_set_lock(omp_lock_t *lock) {
    if (lock->held) {
        fputs("omp_set_lock: lock is already held (deadlock)\n", stderr);
        __builtin_trap();
    }
    lock->held = 1;
}
static inline void omp_unset_lock(omp_lock_t *lock) { lock->held = 0; }
static inline int omp_test_lock(omp_lock_t *lock) {
    if (lock->held)
        return 0;
    lock->held = 1;
    return 1;
}

static inline void omp_init_nest_lock(omp_nest_lock_t *lock) {
    lock->depth = 0;
}
static inline void omp_destroy_nest_lock(omp_nest_lock_t *lock) {
    lock->depth = 0;
}
static inline void omp_set_nest_lock(omp_nest_lock_t *lock) { lock->depth++; }
static inline void omp_unset_nest_lock(omp_nest_lock_t *lock) {
    if (lock->depth > 0)
        lock->depth--;
}
static inline int omp_test_nest_lock(omp_nest_lock_t *lock) {
    return ++lock->depth;
}

#endif /* __CCCC_OMP_THREADED__ */

#endif
