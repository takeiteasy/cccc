/* sys/statvfs.h - filesystem statistics (POSIX) for CCCC
 *
 * struct statvfs diverges hard between the two supported hosts: 64 bytes with
 * 32-bit counters on macOS vs 112 bytes with 64-bit counters on Linux (verified
 * against real headers/probe). Rather than expose either host layout directly,
 * this declares a CCCC-canonical struct in POSIX field order with wide
 * (unsigned long) counters on both platforms; wrap_statvfs/wrap_fstatvfs
 * (src/stdlib/posix_statfs.c) populate a host-local struct via the real
 * statvfs()/fstatvfs() and copy field-by-field, the same shape as
 * wrap_statfs (see sys/mount.h) uses for the non-standard struct statfs.
 */

#ifndef __SYS_STATVFS_H
#define __SYS_STATVFS_H

#ifdef _WIN32
#error "<sys/statvfs.h> is only available on POSIX targets in CCCC"
#endif

/* #1317: this exact file is also what a native/generated re-emission's */
/* replayed `#include <sys/statvfs.h>` resolves to (run_native_backend */
/* forwards -I./include straight through to the host cc, and -I paths are */
/* searched ahead of system directories) -- but `struct statvfs` below is */
/* CCCC's own canonical, wide-counter projection (88 bytes), not the real */
/* host layout (64 bytes/32-bit counters on macOS, 112 bytes/64-bit */
/* counters on Linux), precisely because the two real layouts disagree with */
/* each other. A real host compiler reading this file's projection and */
/* then linking against the real libc statvfs()/fstatvfs() would size any */
/* `sizeof(struct statvfs)`-derived buffer wrong for what those functions */
/* actually write -- copy_statvfs_fields's own f_namemax read (offset 80 in */
/* CCCC's projection) is already past the real 64-byte macOS layout's own */
/* end. Same hazard class, and same fix, as sys/mount.h's struct statfs */
/* (#1031/#1317): guard the whole CCCC-flavored body and hand off to the */
/* host's own <sys/statvfs.h> via #include_next. __CCCC__ is defined */
/* unconditionally by CCCC's own preprocessor before any header is read, so */
/* its absence here means a genuine host compiler is reprocessing this */
/* file -- only possible during -c=native/-c=generated serializer replay. */
#ifdef __CCCC__

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

#else

#include_next <sys/statvfs.h>

#endif /* __CCCC__ */

#endif /* __SYS_STATVFS_H */
