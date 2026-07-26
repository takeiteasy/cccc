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
    long ru_maxrss;
    long ru_ixrss;
    long ru_idrss;
    long ru_isrss;
    long ru_minflt;
    long ru_majflt;
    long ru_nswap;
    long ru_inblock;
    long ru_oublock;
    long ru_msgsnd;
    long ru_msgrcv;
    long ru_nsignals;
    long ru_nvcsw;
    long ru_nivcsw;
};

#define RUSAGE_SELF     0
#define RUSAGE_CHILDREN (-1)

extern int getrusage(int who, struct rusage *usage);

#endif /* __SYS_RESOURCE_H */
