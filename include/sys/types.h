/* sys/types.h - basic system types for JCC */

#ifndef __SYS_TYPES_H
#define __SYS_TYPES_H

#ifdef _WIN32
#error "<sys/types.h> is only available on POSIX targets in JCC"
#endif

#ifdef __APPLE__
typedef int               dev_t;
typedef unsigned long long ino_t;
typedef unsigned short    mode_t;
typedef unsigned short    nlink_t;
typedef unsigned int      uid_t;
typedef unsigned int      gid_t;
typedef long long         off_t;
typedef int               pid_t;
typedef int               blksize_t;
typedef long long         blkcnt_t;
typedef unsigned int      useconds_t;
#else
/* Linux / generic POSIX */
typedef unsigned long     dev_t;
typedef unsigned long     ino_t;
typedef unsigned int      mode_t;
typedef unsigned long     nlink_t;
typedef unsigned int      uid_t;
typedef unsigned int      gid_t;
typedef long              off_t;
typedef int               pid_t;
typedef long              blksize_t;
typedef long              blkcnt_t;
typedef unsigned int      useconds_t;
#endif

#endif /* __SYS_TYPES_H */
