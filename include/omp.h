/*!
 * @file omp.h
 * @brief OpenMP runtime API for `-fopenmp`. See docs/openmp.md.
 *
 * In the VM parallel regions run on one thread, so the team is always the
 * initial thread and locks cannot contend. `-c=native`, `-m` and
 * `-c=generated` run them on real threads with the same API. Documented here
 * is the VM form.
 */

#ifndef __OMP_H
#define __OMP_H

#include <stdio.h>
#include <time.h>
#include <unistd.h>

/*! @brief Loop schedule kinds for omp_set_schedule() and omp_get_schedule(). */
typedef enum omp_sched_t {
    omp_sched_static  = 1,
    omp_sched_dynamic = 2,
    omp_sched_guided  = 3,
    omp_sched_auto    = 4
} omp_sched_t;

/*! @brief Modifier bit that may be or'ed into a schedule kind; it is ignored.
 */
#define omp_sched_monotonic ((omp_sched_t)0x80000000u)

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
void omp_set_schedule(omp_sched_t kind, int chunk);
void omp_get_schedule(omp_sched_t *kind, int *chunk);

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

/*! @brief Simple lock. */
typedef struct {
    int held;
} omp_lock_t;

/*! @brief Nestable lock. */
typedef struct {
    int depth;
} omp_nest_lock_t;

/*! @brief Threads in the current team (1 in the VM). */
static inline int omp_get_num_threads(void) { return 1; }
/*! @brief Upper bound on the next team's size. */
static inline int omp_get_max_threads(void) { return 1; }
/*! @brief Calling thread's index in its team (0 in the VM). */
static inline int omp_get_thread_num(void) { return 0; }
/*! @brief Maximum number of threads the program may use. */
static inline int omp_get_thread_limit(void) { return 1; }
/*! @brief Non-zero inside a parallel region (always 0 in the VM). */
static inline int omp_in_parallel(void) { return 0; }
/*! @brief Online processors on the host. */
static inline int omp_get_num_procs(void) {
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? (int)n : 1;
}
/*! @brief Requests a team size for later regions. Ignored in the VM. */
static inline void omp_set_num_threads(int n) { (void)n; }
/*! @brief Enables or disables dynamic team sizing. Ignored in the VM. */
static inline void omp_set_dynamic(int on) { (void)on; }
/*! @brief Whether dynamic team sizing is on (always 0 in the VM). */
static inline int omp_get_dynamic(void) { return 0; }

static inline int *__omp_schedule_icv(void) {
    static int icv[2] = {omp_sched_static, 0};
    return icv;
}
/*! @brief Sets the schedule `schedule(runtime)` loops use. The VM only stores
 * it. */
static inline void omp_set_schedule(omp_sched_t kind, int chunk) {
    int k = (int)kind & 0x7fffffff;
    if (k >= omp_sched_static && k <= omp_sched_auto) {
        __omp_schedule_icv()[0] = k;
        __omp_schedule_icv()[1] = chunk;
    }
}
/*! @brief Reads the schedule `schedule(runtime)` loops use. */
static inline void omp_get_schedule(omp_sched_t *kind, int *chunk) {
    *kind  = (omp_sched_t)__omp_schedule_icv()[0];
    *chunk = __omp_schedule_icv()[1];
}

/*! @brief Monotonic wall-clock time in seconds. */
static inline double omp_get_wtime(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}
/*! @brief Resolution of omp_get_wtime() in seconds. */
static inline double omp_get_wtick(void) { return 1e-9; }

/*! @brief Initialises a lock as unheld. */
static inline void omp_init_lock(omp_lock_t *lock) { lock->held = 0; }
/*! @brief Destroys a lock. */
static inline void omp_destroy_lock(omp_lock_t *lock) { lock->held = 0; }
/*! @brief Acquires a lock. Re-acquiring a held lock in the VM is a deadlock and traps. */
static inline void omp_set_lock(omp_lock_t *lock) {
    if (lock->held) {
        fputs("omp_set_lock: lock is already held (deadlock)\n", stderr);
        __builtin_trap();
    }
    lock->held = 1;
}
/*! @brief Releases a lock. */
static inline void omp_unset_lock(omp_lock_t *lock) { lock->held = 0; }
/*! @brief Acquires a lock if it is free. Returns non-zero on success. */
static inline int omp_test_lock(omp_lock_t *lock) {
    if (lock->held)
        return 0;
    lock->held = 1;
    return 1;
}

/*! @brief Initialises a nestable lock with depth 0. */
static inline void omp_init_nest_lock(omp_nest_lock_t *lock) {
    lock->depth = 0;
}
/*! @brief Destroys a nestable lock. */
static inline void omp_destroy_nest_lock(omp_nest_lock_t *lock) {
    lock->depth = 0;
}
/*! @brief Acquires a nestable lock, increasing its depth. */
static inline void omp_set_nest_lock(omp_nest_lock_t *lock) { lock->depth++; }
/*! @brief Decreases a nestable lock's depth. */
static inline void omp_unset_nest_lock(omp_nest_lock_t *lock) {
    if (lock->depth > 0)
        lock->depth--;
}
/*! @brief Tries to acquire a nestable lock. Returns its new depth. */
static inline int omp_test_nest_lock(omp_nest_lock_t *lock) {
    return ++lock->depth;
}

#endif /* __CCCC_OMP_THREADED__ */

#endif
