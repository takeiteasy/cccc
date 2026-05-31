/* utime.h - file access and modification times for JCC */

#ifndef __UTIME_H
#define __UTIME_H

#ifdef _WIN32
#error "<utime.h> is only available on POSIX targets in JCC"
#endif

struct utimbuf {
    long actime;
    long modtime;
};

extern int utime(const char *filename, const struct utimbuf *times);

#endif /* __UTIME_H */
