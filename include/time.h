/* time.h - time functions for CCCC C compiler */

#ifndef __TIME_H
#define __TIME_H

#include "stddef.h"
#include "sys/types.h" /* clockid_t */

typedef long clock_t;
typedef long time_t;

#define CLOCKS_PER_SEC 1000000

/* #1282: CLOCK_* ids, injected as __CCCC_CLOCK_*__ by this binary's own
 * compile-time <time.h> (src/preprocess.c's init_time_macros) rather than
 * hand-transcribed -- their numeric values are a real cross-platform skew
 * (Darwin vs. glibc number them differently), the same class of bug #779
 * already fixed for errno codes. Linux-only ids are only defined when the
 * host that built cccc actually had them (#824 no-lossy-emulation policy):
 * using CLOCK_BOOTTIME/CLOCK_TAI on a macOS-built cccc is a compile error,
 * not a silently wrong integer. */
#define CLOCK_REALTIME           __CCCC_CLOCK_REALTIME__
#define CLOCK_MONOTONIC          __CCCC_CLOCK_MONOTONIC__
#define CLOCK_PROCESS_CPUTIME_ID __CCCC_CLOCK_PROCESS_CPUTIME_ID__
#define CLOCK_THREAD_CPUTIME_ID  __CCCC_CLOCK_THREAD_CPUTIME_ID__
#ifdef __CCCC_CLOCK_MONOTONIC_RAW__
#define CLOCK_MONOTONIC_RAW __CCCC_CLOCK_MONOTONIC_RAW__
#endif
#ifdef __CCCC_CLOCK_BOOTTIME__
#define CLOCK_BOOTTIME __CCCC_CLOCK_BOOTTIME__
#endif
#ifdef __CCCC_CLOCK_TAI__
#define CLOCK_TAI __CCCC_CLOCK_TAI__
#endif

/* #1022: `_STRUCT_TIMESPEC` is the real guard both glibc */
/* (bits/types/struct_timespec.h, whose own comment says "Include guard */
/* matches what <linux/time.h> uses") and macOS */
/* (sys/_types/_timespec.h) wrap their own `struct timespec` definition in. */
/* This file is otherwise fully self-contained (unlike #1021/#1040-style */
/* fenv.h/stdio.h, it does NOT hand off to the host's own <time.h> -- */
/* giving the whole file a hand-off was tried and reverted: it dragged in */
/* each platform's real <time.h>, which pulls in far more than struct */
/* timespec alone -- e.g. macOS's real clockid_t collides with the plain */
/* `typedef int clockid_t;` this file's sibling sys/types.h uses, a */
/* collision with no narrow fix; see #1022's own ticket comment). But once */
/* #1022 gave include/pthread.h a real #include_next hand-off, a native */
/* compile of any pthread.h user pulls in the real host <pthread.h>, whose */
/* own internal chain independently reaches the real `struct timespec` */
/* definition too (confirmed: glibc via bits/types/struct_timespec.h, both */
/* on macOS and Linux this file's own copy is found FIRST via -I./include, */
/* since -I is searched for every #include in the TU regardless of which */
/* header issues it) -- a hard "redefinition of 'timespec'" error. Defining */
/* the same guard macro right after this definition makes both platforms' */
/* real headers see it as already provided and skip their own (equivalent) */
/* redefinition, without needing to hand off this entire header. */
#define _STRUCT_TIMESPEC struct timespec
struct timespec {
    time_t tv_sec;
    long   tv_nsec;
};

#define TIME_UTC 1

struct tm {
    int   tm_sec;
    int   tm_min;
    int   tm_hour;
    int   tm_mday;
    int   tm_mon;
    int   tm_year;
    int   tm_wday;
    int   tm_yday;
    int   tm_isdst;
    long  tm_gmtoff;
    char *tm_zone;
};

extern clock_t clock(void);
extern double difftime(time_t time1, time_t time0);
extern time_t mktime(struct tm *timeptr);
extern time_t timegm(struct tm *timeptr);
extern time_t time(time_t *t);
extern int timespec_get(struct timespec *ts, int base);
extern int nanosleep(const struct timespec *req, struct timespec *rem);
/* extern int timespec_getres(struct timespec *ts, int base); */
/* #1282: clockid_t-taking POSIX.1-2001 clock functions -- cccc's own
   src/stdlib/time.c and src/stdlib/pthread.c (CLOCK_REALTIME-based deadline
   polling on macOS, which lacks pthread_mutex_timedlock) already call
   clock_gettime(); a bundled-header gap the self-hosting spike hit. */
extern int clock_gettime(clockid_t clk_id, struct timespec *tp);
extern int clock_settime(clockid_t clk_id, const struct timespec *tp);
extern int clock_getres(clockid_t clk_id, struct timespec *res);
#ifdef __linux__ /* glibc only -- Darwin has no clock_nanosleep() */
extern int clock_nanosleep(clockid_t clk_id, int flags,
                           const struct timespec *req, struct timespec *rem);
#endif
extern char *asctime(const struct tm *tm); /* deprecated */
extern char *ctime(const time_t *timer);   /* deprecated */
/* Re-entrant asctime/ctime: caller supplies a buffer of at least 26 bytes.
   POSIX.1-2008; present on both Darwin and glibc. */
extern char *asctime_r(const struct tm *restrict tm, char *restrict buf);
extern char *ctime_r(const time_t *restrict timer, char *restrict buf);
extern struct tm *gmtime(const time_t *timer);
extern struct tm *gmtime_r(const time_t *timer, struct tm *result);
extern struct tm *localtime(const time_t *timer);
extern struct tm *localtime_r(const time_t *timer, struct tm *result);
extern size_t strftime(char *s, size_t maxsize, const char *format,
                       const struct tm *timeptr);

#endif /* __TIME_H */
