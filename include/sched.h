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
 * getscheduler/rr_get_interval are Linux-only in the real world, so on
 * other hosts they're only declared under --posix-emulation, where they're
 * registered as stubs that always return -1 with errno set to ENOSYS (so
 * portable guest code can still compile and link under the VM). Without
 * the flag they're simply undeclared, matching what a native compiler on
 * the same host would do (#824).
 */

#ifndef __SCHED_H
#define __SCHED_H

#ifdef _WIN32
#error "<sched.h> is only available on POSIX targets in CCCC"
#endif

#include "sys/types.h"
#include "time.h"

/* #1143: this header deliberately does NOT have an #ifdef __CCCC__/ */
/* #include_next hand-off the way fenv.h/pthread.h/sys/mount.h do. A */
/* program that both `#include <sched.h>` directly and uses <pthread.h> */
/* used to collide under -c=native when a user -I named CCCC's own bundled */
/* include dir (this repo's own test harness's `-I./include` is exactly */
/* that) -- the replayed `#include <sched.h>` re-emitted this file's own */
/* struct sched_param, while pthread.h's own #include_next hand-off */
/* (#1022) separately reached the real host's differently-shaped one in */
/* the same TU ("redefinition of sched_param", confirmed with */
/* tests/suites/test_suite_posix.c). Investigated for a hand-off here too, */
/* but the same TU also hits five other collisions from CCCC's own */
/* signal.h/netinet/in.h/netdb.h/locale.h shadowing real host declarations */
/* (SIG_SETMASK, htonl, gethostbyname_r, lconv/locale_t/freelocale) that a */
/* hand-off scoped to this file alone can't reach -- fixed instead at the */
/* root: run_native_backend() (main.c) now demotes any user -I/-isystem */
/* entry that resolved one of CCCC's own bundled headers to `-idirafter`, */
/* so the real host header always wins the search regardless of which */
/* bundled header the collision would otherwise come from. This file */
/* itself is unmodified by that fix (the host cc never reads it under */
/* -c=native once its dir is demoted), so its own struct sched_param stays */
/* exactly this simple, guest-visible 4-byte form -- see the file comment */
/* above for why. */
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

#if defined(__linux__) || defined(__CCCC_POSIX_EMULATION__)
extern int sched_setparam(pid_t pid, const struct sched_param *param);
extern int sched_getparam(pid_t pid, struct sched_param *param);
extern int sched_setscheduler(pid_t pid, int policy,
                              const struct sched_param *param);
extern int sched_getscheduler(pid_t pid);
extern int sched_rr_get_interval(pid_t pid, struct timespec *interval);
#endif

#endif /* __SCHED_H */
