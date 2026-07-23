/* sys/time.h - time types for CCCC */

#ifndef __SYS_TIME_H
#define __SYS_TIME_H

#ifdef _WIN32
#error "<sys/time.h> is only available on POSIX targets in CCCC"
#endif

#include "time.h"

struct timeval {
    long tv_sec;
    long tv_usec;
};

struct timezone {
    int tz_minuteswest;
    int tz_dsttime;
};

extern int gettimeofday(struct timeval *tv, struct timezone *tz);
extern int settimeofday(const struct timeval *tv, const struct timezone *tz);
extern int utimes(const char *path, const struct timeval times[2]);
extern int futimes(int fd, const struct timeval times[2]);
extern int lutimes(const char *path, const struct timeval times[2]);

struct itimerval {
    struct timeval it_interval;
    struct timeval it_value;
};

#define ITIMER_REAL    0
#define ITIMER_VIRTUAL 1
#define ITIMER_PROF    2

extern int setitimer(int which, const struct itimerval *new_value, struct itimerval *old_value);
extern int getitimer(int which, struct itimerval *curr_value);

#define timeradd(a, b, res) do { \
    (res)->tv_sec = (a)->tv_sec + (b)->tv_sec; \
    (res)->tv_usec = (a)->tv_usec + (b)->tv_usec; \
    if ((res)->tv_usec >= 1000000) { \
        (res)->tv_sec++; \
        (res)->tv_usec -= 1000000; \
    } \
} while (0)

#define timersub(a, b, res) do { \
    (res)->tv_sec = (a)->tv_sec - (b)->tv_sec; \
    (res)->tv_usec = (a)->tv_usec - (b)->tv_usec; \
    if ((res)->tv_usec < 0) { \
        (res)->tv_sec--; \
        (res)->tv_usec += 1000000; \
    } \
} while (0)

#endif /* __SYS_TIME_H */
