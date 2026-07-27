/* sched.h - execution scheduling for CCCC
 *
 * SCHED_OTHER/FIFO/RR genuinely disagree between hosts (macOS: 1/4/2, Linux:
 * 0/1/2 -- verified against real headers), so CCCC declares its own canonical
 * numbering and wrap_sched_setscheduler/wrap_sched_getscheduler/
 * wrap_sched_get_priority_min/max (src/stdlib/posix.c) translate to the real
 * host value before the call and back on the way out.
 *
 * struct sched_param is also host-divergent: 8 bytes on macOS (an int plus a
 * 4-byte __opaque tail used internally by libpthread) vs 4 bytes on Linux (a
 * bare int). The guest-visible struct here is the POSIX-minimal 4-byte form;
 * wrap_sched_setparam/getparam marshal through a host-sized local struct
 * sched_param rather than handing the guest pointer straight to the host, so
 * the host's extra tail bytes on macOS never touch guest memory.
 *
 * Darwin's real <sched.h> only declares sched_yield() and
 * sched_get_priority_min/max() -- it has no process-scheduling API at all
 * (verified against the SDK header). sched_setparam/getparam/setscheduler/
 * getscheduler/rr_get_interval are Linux-only in the real world; on macOS
 * they're still registered here (so portable guest code compiles and links
 * on both platforms) but always return -1 with errno set to ENOSYS.
 */

#ifndef __SCHED_H
#define __SCHED_H

#ifdef _WIN32
#error "<sched.h> is only available on POSIX targets in CCCC"
#endif

#include "sys/types.h"
#include "time.h"

struct sched_param {
    int sched_priority;
};

/* CCCC-canonical scheduling policy numbering, translated to the host's real
   values by the wrappers in src/stdlib/posix.c. */
#define SCHED_OTHER 0
#define SCHED_FIFO  1
#define SCHED_RR    2

#ifdef __linux__
#define SCHED_BATCH 3
#define SCHED_IDLE  5
#endif

extern int sched_yield(void);
extern int sched_get_priority_min(int policy);
extern int sched_get_priority_max(int policy);

extern int sched_setparam(pid_t pid, const struct sched_param *param);
extern int sched_getparam(pid_t pid, struct sched_param *param);
extern int sched_setscheduler(pid_t pid, int policy, const struct sched_param *param);
extern int sched_getscheduler(pid_t pid);
extern int sched_rr_get_interval(pid_t pid, struct timespec *interval);

#endif /* __SCHED_H */
