/* time.h - time functions for CCCC C compiler */

#ifndef __TIME_H
#define __TIME_H

#include "stddef.h"

typedef long clock_t;
typedef long time_t;

#define CLOCKS_PER_SEC 1000000

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
extern char *asctime(const struct tm *tm); /* deprecated */
extern char *ctime(const time_t *timer);   /* deprecated */
extern struct tm *gmtime(const time_t *timer);
extern struct tm *gmtime_r(const time_t *timer, struct tm *result);
extern struct tm *localtime(const time_t *timer);
extern struct tm *localtime_r(const time_t *timer, struct tm *result);
extern size_t strftime(char *s, size_t maxsize, const char *format,
                       const struct tm *timeptr);

#endif /* __TIME_H */
