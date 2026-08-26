/* sys/times.h - process times declarations for CCCC */

#ifndef __SYS_TIMES_H
#define __SYS_TIMES_H

#ifdef _WIN32
#error "<sys/times.h> is only available on POSIX targets in CCCC"
#endif

#include "../time.h" /* for clock_t */

/* struct tms (#733/#737). Layout is identical on macOS and Linux -- verified
   via sizeof/offsetof against real macOS and Linux x86_64/aarch64 headers
   (Linux values match across x86_64/aarch64): sizeof(struct tms) == 32 on
   both, all four fields plain 8-byte clock_t at offsets 0/8/16/24. */
struct tms {
    clock_t tms_utime;
    clock_t tms_stime;
    clock_t tms_cutime;
    clock_t tms_cstime;
};

extern clock_t times(struct tms *buf);

#endif /* __SYS_TIMES_H */
