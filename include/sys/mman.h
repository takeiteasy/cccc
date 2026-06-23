/* sys/mman.h - memory management declarations for CCCC */

#ifndef __SYS_MMAN_H
#define __SYS_MMAN_H

#ifdef _WIN32
#error "<sys/mman.h> is only available on POSIX targets in CCCC"
#endif

#include "unistd.h"

#define PROT_NONE  0x00
#define PROT_READ  0x01
#define PROT_WRITE 0x02
#define PROT_EXEC  0x04

#define MAP_FAILED ((void *)-1)

#ifdef __APPLE__
#define MAP_SHARED    0x0001
#define MAP_PRIVATE   0x0002
#define MAP_FIXED     0x0010
#define MAP_ANON      0x1000
#define MAP_ANONYMOUS MAP_ANON
#else
#define MAP_SHARED    0x01
#define MAP_PRIVATE   0x02
#define MAP_FIXED     0x10
#define MAP_ANONYMOUS 0x20
#define MAP_ANON      MAP_ANONYMOUS
#endif

#define MS_ASYNC      0x0001
#define MS_SYNC       0x0002
#define MS_INVALIDATE 0x0004

#define MADV_NORMAL     0
#define MADV_RANDOM     1
#define MADV_SEQUENTIAL 2
#define MADV_WILLNEED   3
#define MADV_DONTNEED   4

extern void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
extern int munmap(void *addr, size_t length);
extern int mprotect(void *addr, size_t len, int prot);
extern int msync(void *addr, size_t len, int flags);
extern int posix_madvise(void *addr, size_t len, int advice);

#endif /* __SYS_MMAN_H */
