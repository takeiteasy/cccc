/* errno.h - error codes for CCCC C compiler */

#ifndef __ERRNO_H
#define __ERRNO_H

extern int errno;

/* Standard C error codes */
#define EPERM    1
#define ENOENT   2
#define ESRCH    3
#define EINTR    4
#define EIO      5
#define ENXIO    6
#define E2BIG    7
#define ENOEXEC  8
#define EBADF    9
#define ECHILD   10
#define EDEADLK  11
#define ENOMEM   12
#define EACCES   13
#define EFAULT   14
#define EBUSY    16
#define EEXIST   17
#define EXDEV    18
#define ENODEV   19
#define ENOTDIR  20
#define EISDIR   21
#define EINVAL   22
#define ENFILE   23
#define EMFILE   24
#define ENOTTY   25
#define EFBIG    27
#define ENOSPC   28
#define ESPIPE   29
#define EROFS    30
#define EMLINK   31
#define EPIPE    32
#define EDOM     33
#define ERANGE   34
#define EAGAIN   35

/* POSIX error codes (platform-specific where they differ) */
#ifdef __APPLE__
#  define EWOULDBLOCK  35
#  define EINPROGRESS  36
#  define EALREADY     37
#  define ENOTSOCK     38
#  define EDESTADDRREQ 39
#  define EMSGSIZE     40
#  define EPROTOTYPE   41
#  define ENOPROTOOPT  42
#  define ENOTSUP      45
#  define EAFNOSUPPORT 47
#  define EADDRINUSE   48
#  define EADDRNOTAVAIL 49
#  define ENETDOWN     50
#  define ENETUNREACH  51
#  define ECONNABORTED 53
#  define ECONNRESET   54
#  define ENOBUFS      55
#  define EISCONN      56
#  define ENOTCONN     57
#  define ETIMEDOUT    60
#  define ECONNREFUSED 61
#  define ELOOP        62
#  define ENAMETOOLONG 63
#  define EHOSTUNREACH 65
#  define ENOTEMPTY    66
#  define ENOSYS       78
#  define EOVERFLOW    84
#  define ENOTSUP      45
#  define EILSEQ       92
#  define ENOLINK      97
#  define EPROTO       100
#else
#  define EWOULDBLOCK  11
#  define EINPROGRESS  115
#  define EALREADY     114
#  define ENOTSOCK     88
#  define EDESTADDRREQ 89
#  define EMSGSIZE     90
#  define EPROTOTYPE   91
#  define ENOPROTOOPT  92
#  define EAFNOSUPPORT 97
#  define EADDRINUSE   98
#  define EADDRNOTAVAIL 99
#  define ENETDOWN     100
#  define ENETUNREACH  101
#  define ECONNABORTED 103
#  define ECONNRESET   104
#  define ENOBUFS      105
#  define EISCONN      106
#  define ENOTCONN     107
#  define ETIMEDOUT    110
#  define ECONNREFUSED 111
#  define ELOOP        40
#  define ENAMETOOLONG 36
#  define EHOSTUNREACH 113
#  define ENOTEMPTY    39
#  define ENOSYS       38
#  define EOVERFLOW    75
#  define ENOTSUP      95
#  define EILSEQ       84
#  define ENOLINK      67
#  define EPROTO       71
#endif

#endif /* __ERRNO_H */
