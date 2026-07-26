/* sys/uio.h - scatter/gather I/O declarations for CCCC */

#ifndef __SYS_UIO_H
#define __SYS_UIO_H

#ifdef _WIN32
#error "<sys/uio.h> is only available on POSIX targets in CCCC"
#endif

#include "stddef.h"

#ifndef _SSIZE_T
typedef long ssize_t;
#define _SSIZE_T
#endif

struct iovec {
    void *iov_base;
    size_t iov_len;
};

extern ssize_t readv(int fd, const struct iovec *iov, int iovcnt);
extern ssize_t writev(int fd, const struct iovec *iov, int iovcnt);

#endif /* __SYS_UIO_H */
