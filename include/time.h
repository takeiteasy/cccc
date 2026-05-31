/* time.h - time functions for JCC C compiler */

#ifndef __TIME_H
#define __TIME_H

#include "stddef.h"

typedef long clock_t;
typedef long time_t;

struct timespec {
    time_t tv_sec;
    long tv_nsec;
};

#define TIME_UTC 1

struct tm {
    int tm_sec;
    int tm_min;
    int tm_hour;
    int tm_mday;
    int tm_mon;
    int tm_year;
    int tm_wday;
    int tm_yday;
    int tm_isdst;
};

extern clock_t clock(void);
extern double difftime(time_t time1, time_t time0);
extern time_t mktime(struct tm *timeptr);
// extern time_t timegm(const struct tm *timeptr);
extern time_t time(time_t *t);
extern int timespec_get(struct timespec *ts, int base);
// extern int timespec_getres(struct timespec *ts, int base);
extern char *asctime(const struct tm *tm); // deprecated
extern char *ctime(const time_t *timer); // deprecated
extern struct tm *gmtime(const time_t *timer);
extern struct tm *gmtime_r(const time_t *timer, struct tm *result);
extern struct tm *localtime(const time_t *timer);
extern struct tm *localtime_r(const time_t *timer, struct tm *result);
extern size_t strftime(char *s, size_t maxsize, const char *format, const struct tm *timeptr);

#endif /* __TIME_H */
