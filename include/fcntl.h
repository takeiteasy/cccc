/* fcntl.h - core POSIX file-control declarations for JCC */

#ifndef __FCNTL_H
#define __FCNTL_H

#ifdef _WIN32
#error "<fcntl.h> is only available on POSIX targets in JCC"
#endif

#include "unistd.h"

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
#else
#define O_CREAT 0100
#define O_EXCL 0200
#define O_TRUNC 01000
#define O_APPEND 02000
#define O_NONBLOCK 04000
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
