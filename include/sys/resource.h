/* sys/resource.h - resource usage declarations for CCCC */

#ifndef __SYS_RESOURCE_H
#define __SYS_RESOURCE_H

#ifdef _WIN32
#error "<sys/resource.h> is only available on POSIX targets in CCCC"
#endif

#include "sys/time.h"

/* struct rusage (#747), needed by getrusage() and wait3()/wait4(). Layout is
   identical on macOS and Linux -- verified via sizeof/offsetof against real
   macOS and Linux x86_64/aarch64 headers (Linux values match across
   x86_64/aarch64; sizeof(struct rusage) == 144 on both, all fourteen
   counters after ru_utime/ru_stime are plain `long`). ru_maxrss's *unit*
   still diverges even though the field's offset/width don't: it is measured
   in bytes on macOS but kilobytes on Linux -- a real semantic difference,
   not something this header can paper over. */
struct rusage {
    struct timeval ru_utime;
    struct timeval ru_stime;
    long           ru_maxrss;
    long           ru_ixrss;
    long           ru_idrss;
    long           ru_isrss;
    long           ru_minflt;
    long           ru_majflt;
    long           ru_nswap;
    long           ru_inblock;
    long           ru_oublock;
    long           ru_msgsnd;
    long           ru_msgrcv;
    long           ru_nsignals;
    long           ru_nvcsw;
    long           ru_nivcsw;
};

#define RUSAGE_SELF     0
#define RUSAGE_CHILDREN (-1)
#ifndef __APPLE__
/* RUSAGE_THREAD is Linux-only; macOS has no per-thread getrusage(). */
#define RUSAGE_THREAD 1
#endif

extern int getrusage(int who, struct rusage *usage);

/* struct rlimit / RLIMIT_* / getrlimit()/setrlimit() (#786). Unlike struct
   rusage above, the *numbering* of RLIMIT_* genuinely diverges between
   macOS and Linux (not just extra Linux-only entries) -- verified via
   sizeof/offsetof/constant probes against real macOS and Linux
   x86_64/aarch64 headers (Linux values match across x86_64/aarch64;
   sizeof(struct rlimit) == 16 on both, rlim_cur/rlim_max both plain
   64-bit rlim_t at offsets 0/8). Notably RLIMIT_AS aliases RLIMIT_RSS on
   macOS (both 5) but is a distinct value (9) on Linux. RLIM_INFINITY also
   diverges: macOS uses INT64_MAX-as-unsigned, Linux uses UINT64_MAX. */
typedef unsigned long long rlim_t;

struct rlimit {
    rlim_t rlim_cur;
    rlim_t rlim_max;
};

#ifdef __APPLE__
#define RLIMIT_CPU     0
#define RLIMIT_FSIZE   1
#define RLIMIT_DATA    2
#define RLIMIT_STACK   3
#define RLIMIT_CORE    4
#define RLIMIT_AS      5
#define RLIMIT_RSS     5
#define RLIMIT_MEMLOCK 6
#define RLIMIT_NPROC   7
#define RLIMIT_NOFILE  8
#define RLIM_NLIMITS   9
#define RLIM_INFINITY  ((rlim_t)0x7fffffffffffffffULL)
#else
#define RLIMIT_CPU        0
#define RLIMIT_FSIZE      1
#define RLIMIT_DATA       2
#define RLIMIT_STACK      3
#define RLIMIT_CORE       4
#define RLIMIT_RSS        5
#define RLIMIT_NPROC      6
#define RLIMIT_NOFILE     7
#define RLIMIT_MEMLOCK    8
#define RLIMIT_AS         9
#define RLIMIT_LOCKS      10
#define RLIMIT_SIGPENDING 11
#define RLIMIT_MSGQUEUE   12
#define RLIMIT_NICE       13
#define RLIMIT_RTPRIO     14
#define RLIMIT_RTTIME     15
#define RLIM_NLIMITS      16
#define RLIM_INFINITY     ((rlim_t)0xffffffffffffffffULL)
#endif
#define RLIMIT_NLIMITS RLIM_NLIMITS

extern int getrlimit(int resource, struct rlimit *rlim);
extern int setrlimit(int resource, const struct rlimit *rlim);

/* getpriority()/setpriority() (#786). PRIO_* values are identical on macOS
   and Linux -- verified against real headers. getpriority() legitimately
   returns -1 on success (a valid nice value), so callers must clear errno
   before the call and check it afterward rather than testing the return
   value alone. */
#define PRIO_PROCESS 0
#define PRIO_PGRP    1
#define PRIO_USER    2

extern int getpriority(int which, int who);
extern int setpriority(int which, int who, int prio);

#endif /* __SYS_RESOURCE_H */
