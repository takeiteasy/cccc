/* fcntl.h - core POSIX file-control declarations for CCCC */

#ifndef __FCNTL_H
#define __FCNTL_H

#ifdef _WIN32
#error "<fcntl.h> is only available on POSIX targets in CCCC"
#endif

#include "unistd.h"
#include "sys/types.h"

#define O_RDONLY 0x0000
#define O_WRONLY 0x0001
#define O_RDWR 0x0002
#define O_ACCMODE 0x0003

#ifdef __APPLE__
#define O_NONBLOCK 0x0004
#define O_APPEND 0x0008
#define O_CREAT 0x0200
#define O_TRUNC 0x0400
#define O_EXCL 0x0800
#define O_CLOEXEC 0x1000000
#else
#define O_CREAT 0100
#define O_EXCL 0200
#define O_TRUNC 01000
#define O_APPEND 02000
#define O_NONBLOCK 04000
#define O_CLOEXEC 02000000
#endif

/* fcntl() commands and record-locking constants (platform-specific) */
#define F_DUPFD 0
#define F_GETFD 1
#define F_SETFD 2
#define F_GETFL 3
#define F_SETFL 4

#define FD_CLOEXEC 1

#ifdef __APPLE__
#define F_GETLK  7
#define F_SETLK  8
#define F_SETLKW 9
#define F_RDLCK  1
#define F_UNLCK  2
#define F_WRLCK  3
#define F_FULLFSYNC 51   /* Darwin: flush buffers to physical media */

struct flock {
    off_t l_start;   /* starting offset */
    off_t l_len;     /* len = 0 means until end of file */
    pid_t l_pid;     /* lock owner */
    short l_type;    /* lock type: read/write, etc. */
    short l_whence;  /* type of l_start */
};
#else
#define F_GETLK  5
#define F_SETLK  6
#define F_SETLKW 7
#define F_RDLCK  0
#define F_WRLCK  1
#define F_UNLCK  2

struct flock {
    short l_type;    /* lock type: read/write, etc. */
    short l_whence;  /* type of l_start */
    off_t l_start;   /* starting offset */
    off_t l_len;     /* len = 0 means until end of file */
    pid_t l_pid;     /* lock owner */
};
#endif

#define S_IRUSR 0400
#define S_IWUSR 0200
#define S_IXUSR 0100
#define S_IRGRP 0040
#define S_IWGRP 0020
#define S_IXGRP 0010
#define S_IROTH 0004
#define S_IWOTH 0002
#define S_IXOTH 0001

extern int open(const char *path, int oflag, ...);
extern int creat(const char *path, unsigned int mode);
extern int fcntl(int fd, int cmd, ...);

#endif /* __FCNTL_H */
