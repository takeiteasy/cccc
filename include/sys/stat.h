/* sys/stat.h - file status for CCCC */

#ifndef __SYS_STAT_H
#define __SYS_STAT_H

#ifdef _WIN32
#error "<sys/stat.h> is only available on POSIX targets in CCCC"
#endif

#include "sys/types.h"

/* Pull in struct timespec and time_t */
#include "../time.h"

#ifdef __APPLE__

struct stat {
    dev_t           st_dev;
    mode_t          st_mode;
    nlink_t         st_nlink;
    ino_t           st_ino;
    uid_t           st_uid;
    gid_t           st_gid;
    dev_t           st_rdev;
    struct timespec st_atimespec;
    struct timespec st_mtimespec;
    struct timespec st_ctimespec;
    struct timespec st_birthtimespec;
    off_t           st_size;
    blkcnt_t        st_blocks;
    blksize_t       st_blksize;
    unsigned int    st_flags;
    unsigned int    st_gen;
    int             st_lspare;
    long long       st_qspare[2];
};

#elif defined(__x86_64__)
/* Linux x86_64 glibc layout (sizeof==144, offsetof(st_size)==48,
 * offsetof(st_mtim)==88) */

struct stat {
    dev_t           st_dev;
    ino_t           st_ino;
    nlink_t         st_nlink; /* nlink precedes mode on x86_64 */
    mode_t          st_mode;
    uid_t           st_uid;
    gid_t           st_gid;
    int             __pad0;
    dev_t           st_rdev;
    off_t           st_size;
    blksize_t       st_blksize; /* long (8 bytes) on x86_64 */
    blkcnt_t        st_blocks;
    struct timespec st_atim;
    struct timespec st_mtim;
    struct timespec st_ctim;
    long            __glibc_reserved[3];
};

_Static_assert(sizeof(struct stat) == 144,
               "x86_64 struct stat layout mismatch");

#else
/* Linux aarch64 / generic POSIX */

struct stat {
    dev_t           st_dev;
    ino_t           st_ino;
    mode_t          st_mode;
    nlink_t         st_nlink;
    uid_t           st_uid;
    gid_t           st_gid;
    dev_t           st_rdev;
    dev_t           __pad1;
    off_t           st_size;
    blksize_t       st_blksize;
    int             __pad2;
    blkcnt_t        st_blocks;
    struct timespec st_atim;
    struct timespec st_mtim;
    struct timespec st_ctim;
    int             __glibc_reserved[2];
};

#endif

/* POSIX's plain st_atime/st_mtime/st_ctime are macros aliasing the tv_sec
 * field of the finer-grained timespec member above -- both real Darwin and
 * glibc provide them this way (Darwin via st_mtimespec.tv_sec, glibc via
 * st_mtim.tv_sec), never as their own struct field. */
#ifdef __APPLE__
#define st_atime st_atimespec.tv_sec
#define st_mtime st_mtimespec.tv_sec
#define st_ctime st_ctimespec.tv_sec
#else
#define st_atime st_atim.tv_sec
#define st_mtime st_mtim.tv_sec
#define st_ctime st_ctim.tv_sec
#endif

#define S_IFMT      0xF000
#define S_IFIFO     0x1000
#define S_IFCHR     0x2000
#define S_IFDIR     0x4000
#define S_IFBLK     0x6000
#define S_IFREG     0x8000
#define S_IFLNK     0xA000
#define S_IFSOCK    0xC000

#define S_ISFIFO(m) (((m) & S_IFMT) == S_IFIFO)
#define S_ISCHR(m)  (((m) & S_IFMT) == S_IFCHR)
#define S_ISDIR(m)  (((m) & S_IFMT) == S_IFDIR)
#define S_ISBLK(m)  (((m) & S_IFMT) == S_IFBLK)
#define S_ISREG(m)  (((m) & S_IFMT) == S_IFREG)
#define S_ISLNK(m)  (((m) & S_IFMT) == S_IFLNK)
#define S_ISSOCK(m) (((m) & S_IFMT) == S_IFSOCK)

#define S_IRWXU     0700
#define S_IRUSR     0400
#define S_IWUSR     0200
#define S_IXUSR     0100
#define S_IRWXG     0070
#define S_IRGRP     0040
#define S_IWGRP     0020
#define S_IXGRP     0010
#define S_IRWXO     0007
#define S_IROTH     0004
#define S_IWOTH     0002
#define S_IXOTH     0001

#define S_ISUID     04000
#define S_ISGID     02000
#define S_ISVTX     01000

/* Sentinel tv_nsec values for utimensat()/futimens() */
#ifdef __APPLE__
#define UTIME_NOW  -1
#define UTIME_OMIT -2
#else
#define UTIME_NOW  0x3FFFFFFF
#define UTIME_OMIT 0x3FFFFFFE
#endif

/* *at() family: fd + flag constants (verified against real macOS and Linux
   x86_64/aarch64 headers -- Linux x86_64/aarch64 values match each other). */
#ifdef __APPLE__
#define AT_FDCWD            -2
#define AT_SYMLINK_NOFOLLOW 0x0020
#define AT_REMOVEDIR        0x0080
#else
#define AT_FDCWD            -100
#define AT_SYMLINK_NOFOLLOW 0x100
#define AT_REMOVEDIR        0x200
#endif

extern int stat(const char *path, struct stat *buf);
extern int fstat(int fd, struct stat *buf);
extern int lstat(const char *path, struct stat *buf);
extern int fstatat(int fd, const char *path, struct stat *buf, int flag);
extern int chmod(const char *path, mode_t mode);
extern int fchmod(int fd, mode_t mode);
extern int fchmodat(int fd, const char *path, mode_t mode, int flag);
extern int mkdir(const char *path, mode_t mode);
extern int mkdirat(int fd, const char *path, mode_t mode);
extern int mkfifo(const char *path, mode_t mode);
extern int mknod(const char *path, mode_t mode, dev_t dev);
extern mode_t umask(mode_t cmask);

#endif /* __SYS_STAT_H */
