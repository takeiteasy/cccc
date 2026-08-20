/* sys/types.h - basic system types for CCCC */

#ifndef __SYS_TYPES_H
#define __SYS_TYPES_H

#ifdef _WIN32
#error "<sys/types.h> is only available on POSIX targets in CCCC"
#endif

#ifdef __APPLE__
typedef int                dev_t;
typedef unsigned long long ino_t;
typedef unsigned short     mode_t;
typedef unsigned short     nlink_t;
typedef unsigned int       uid_t;
typedef unsigned int       gid_t;
typedef long long          off_t;
typedef int                pid_t;
typedef int                blksize_t;
typedef long long          blkcnt_t;
typedef unsigned int       useconds_t;
typedef unsigned char      sa_family_t;
typedef unsigned int       socklen_t;
typedef int                clockid_t;
typedef int                key_t;
typedef unsigned int       id_t;
typedef unsigned int       fsblkcnt_t;
typedef unsigned int       fsfilcnt_t;
typedef int                nl_item;
/* timer_t (POSIX timer_create/timer_settime API) intentionally omitted:
   macOS does not implement this API, so there is no host type to alias. */
#else
/* Linux / generic POSIX */
typedef unsigned long dev_t;
typedef unsigned long ino_t;
typedef unsigned int  mode_t;
#if defined(__aarch64__)
typedef unsigned int nlink_t;
#else
typedef unsigned long nlink_t;
#endif
typedef unsigned int uid_t;
typedef unsigned int gid_t;
typedef long         off_t;
typedef int          pid_t;
#if defined(__aarch64__)
typedef int blksize_t;
#else
typedef long blksize_t;
#endif
typedef long           blkcnt_t;
typedef unsigned int   useconds_t;
typedef unsigned short sa_family_t;
typedef unsigned int   socklen_t;
typedef int            clockid_t;
// #1022: `__clockid_t` (leading-underscore, glibc's own internal name for
// the same type `clockid_t` is a public alias of) is needed by real glibc
// <pthread.h> once include/pthread.h hands off to it (#1022) --
// pthread_mutex_clocklock()/pthread_cond_clockwait() (glibc-only extensions,
// declared unconditionally in glibc's own header) spell their clockid_t
// parameter with the private name. Since -I./include shadows glibc's own
// <sys/types.h>/<bits/types.h> (which would otherwise supply it) for every
// #include in the TU, not just ones this file itself issues, it has to be
// supplied here too. Not needed on the __APPLE__ side: Apple's own
// <pthread.h> has no such glibc-only extension to reach.
typedef int           __clockid_t;
typedef void         *timer_t;
typedef int           key_t;
typedef unsigned int  id_t;
typedef unsigned long fsblkcnt_t;
typedef unsigned long fsfilcnt_t;
typedef int           nl_item;
#endif

#endif /* __SYS_TYPES_H */
