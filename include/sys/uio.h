/* sys/uio.h - scatter/gather I/O declarations for CCCC */

#ifndef __SYS_UIO_H
#define __SYS_UIO_H

#ifdef _WIN32
#error "<sys/uio.h> is only available on POSIX targets in CCCC"
#endif

#include "stddef.h"
#include "sys/types.h"

#ifndef _SSIZE_T
typedef long ssize_t;
#define _SSIZE_T
#endif

struct iovec {
    void  *iov_base;
    size_t iov_len;
};

extern ssize_t readv(int fd, const struct iovec *iov, int iovcnt);
extern ssize_t writev(int fd, const struct iovec *iov, int iovcnt);

/* preadv/pwritev (POSIX.1-2008) -- the readv/writev analogs of pread/pwrite:
   scatter/gather I/O at an explicit file offset, without disturbing the
   fd's own file position. Both hosts have these natively (macOS since
   11.0, verified against the SDK header; Linux since kernel 2.6.30/glibc
   2.10), so unlike preadv2/pwritev2 below they need no __linux__ gate
   (#793). */
extern ssize_t preadv(int fd, const struct iovec *iov, int iovcnt,
                      off_t offset);
extern ssize_t pwritev(int fd, const struct iovec *iov, int iovcnt,
                       off_t offset);

#ifdef __linux__
/* preadv2/pwritev2 -- same as preadv/pwritev plus an RWF_* flags word
   (Linux-only syscalls, glibc >= 2.26; no macOS equivalent at all, unlike
   preadv/pwritev above). Flag values verified against real
   <linux/fs.h>/<bits/uio-ext.h> (identical on x86_64/aarch64). Same
   Linux-only-extension pattern as mremap/fallocate/splice
   (src/stdlib/posix.c) (#793). */
#define RWF_HIPRI  0x00000001
#define RWF_DSYNC  0x00000002
#define RWF_SYNC   0x00000004
#define RWF_NOWAIT 0x00000008
#define RWF_APPEND 0x00000010

extern ssize_t preadv2(int fd, const struct iovec *iov, int iovcnt,
                       off_t offset, int flags);
extern ssize_t pwritev2(int fd, const struct iovec *iov, int iovcnt,
                        off_t offset, int flags);
#endif

#endif /* __SYS_UIO_H */
