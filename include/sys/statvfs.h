/* sys/statvfs.h - filesystem statistics (POSIX) for CCCC
 *
 * struct statvfs diverges hard between the two supported hosts: 64 bytes with
 * 32-bit counters on macOS vs 112 bytes with 64-bit counters on Linux (verified
 * against real headers/probe). Rather than expose either host layout directly,
 * this declares a CCCC-canonical struct in POSIX field order with wide
 * (unsigned long) counters on both platforms; wrap_statvfs/wrap_fstatvfs
 * (src/stdlib/posix.c) populate a host-local struct via the real statvfs()/
 * fstatvfs() and copy field-by-field, the same shape as wrap_statfs (see
 * sys/mount.h) uses for the non-standard struct statfs.
 */

#ifndef __SYS_STATVFS_H
#define __SYS_STATVFS_H

#ifdef _WIN32
#error "<sys/statvfs.h> is only available on POSIX targets in CCCC"
#endif

struct statvfs {
    unsigned long f_bsize;
    unsigned long f_frsize;
    unsigned long f_blocks;
    unsigned long f_bfree;
    unsigned long f_bavail;
    unsigned long f_files;
    unsigned long f_ffree;
    unsigned long f_favail;
    unsigned long f_fsid;
    unsigned long f_flag;
    unsigned long f_namemax;
};

/* f_flag bits -- identical numbering on macOS and Linux. */
#define ST_RDONLY 1
#define ST_NOSUID 2

#ifdef __linux__
#define ST_NODEV       4
#define ST_NOEXEC      8
#define ST_SYNCHRONOUS 16
#endif

extern int statvfs(const char *path, struct statvfs *buf);
extern int fstatvfs(int fd, struct statvfs *buf);

#endif /* __SYS_STATVFS_H */
