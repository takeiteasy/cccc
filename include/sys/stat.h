/* sys/stat.h - file status for JCC */

#ifndef __SYS_STAT_H
#define __SYS_STAT_H

#ifdef _WIN32
#error "<sys/stat.h> is only available on POSIX targets in JCC"
#endif

#include "sys/types.h"

/* Pull in struct timespec and time_t */
#include "../time.h"

#ifdef __APPLE__

struct stat {
    dev_t     st_dev;
    mode_t    st_mode;
    nlink_t   st_nlink;
    ino_t     st_ino;
    uid_t     st_uid;
    gid_t     st_gid;
    dev_t     st_rdev;
    struct timespec st_atimespec;
    struct timespec st_mtimespec;
    struct timespec st_ctimespec;
    struct timespec st_birthtimespec;
    off_t     st_size;
    blkcnt_t  st_blocks;
    blksize_t st_blksize;
    unsigned int st_flags;
    unsigned int st_gen;
    int       st_lspare;
    long long st_qspare[2];
};

#else
/* Linux / generic POSIX */

struct stat {
    unsigned long st_dev;
    unsigned long st_ino;
    unsigned long st_nlink;
    unsigned int  st_mode;
    unsigned int  st_uid;
    unsigned int  st_gid;
    unsigned int  __pad0;
    unsigned long st_rdev;
    long          st_size;
    long          st_blksize;
    long          st_blocks;
    struct timespec st_atim;
    struct timespec st_mtim;
    struct timespec st_ctim;
    long          __unused[3];
};

#endif

#define S_IFMT   0xF000
#define S_IFIFO  0x1000
#define S_IFCHR  0x2000
#define S_IFDIR  0x4000
#define S_IFBLK  0x6000
#define S_IFREG  0x8000
#define S_IFLNK  0xA000
#define S_IFSOCK 0xC000

#define S_ISFIFO(m) (((m) & S_IFMT) == S_IFIFO)
#define S_ISCHR(m)  (((m) & S_IFMT) == S_IFCHR)
#define S_ISDIR(m)  (((m) & S_IFMT) == S_IFDIR)
#define S_ISBLK(m)  (((m) & S_IFMT) == S_IFBLK)
#define S_ISREG(m)  (((m) & S_IFMT) == S_IFREG)
#define S_ISLNK(m)  (((m) & S_IFMT) == S_IFLNK)
#define S_ISSOCK(m) (((m) & S_IFMT) == S_IFSOCK)

#define S_IRWXU  0700
#define S_IRUSR  0400
#define S_IWUSR  0200
#define S_IXUSR  0100
#define S_IRWXG  0070
#define S_IRGRP  0040
#define S_IWGRP  0020
#define S_IXGRP  0010
#define S_IRWXO  0007
#define S_IROTH  0004
#define S_IWOTH  0002
#define S_IXOTH  0001

#define S_ISUID  04000
#define S_ISGID  02000
#define S_ISVTX  01000

extern int stat(const char *path, struct stat *buf);
extern int fstat(int fd, struct stat *buf);
extern int lstat(const char *path, struct stat *buf);
extern int chmod(const char *path, mode_t mode);
extern int mkdir(const char *path, mode_t mode);
extern int mkfifo(const char *path, mode_t mode);
extern mode_t umask(mode_t cmask);

#endif /* __SYS_STAT_H */
