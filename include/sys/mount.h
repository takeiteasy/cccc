/* sys/mount.h - filesystem statistics for CCCC
 *
 * Minimal declarations for statfs()/fstatfs(). The full `struct statfs`
 * layout is highly platform-specific; only the fields portable code commonly
 * reads (block size, flags) are exposed here. Code that needs the complete
 * native layout should query it via the host C library directly.
 */

#ifndef __SYS_MOUNT_H
#define __SYS_MOUNT_H

#ifdef _WIN32
#error "<sys/mount.h> is only available on POSIX targets in CCCC"
#endif

#include "sys/types.h"

#ifndef MFSTYPENAMELEN
#define MFSTYPENAMELEN 16
#endif

struct statfs {
    unsigned int  f_bsize;   /* fundamental file system block size */
    unsigned int  f_iosize;  /* optimal transfer block size */
    unsigned long f_blocks;  /* total data blocks in file system */
    unsigned long f_bfree;   /* free blocks in fs */
    unsigned long f_bavail;  /* free blocks avail to non-superuser */
    unsigned long f_files;   /* total file nodes in file system */
    unsigned long f_ffree;   /* free file nodes in fs */
    unsigned int  f_flags;   /* copy of mount exported flags */
};

extern int statfs(const char *path, struct statfs *buf);
extern int fstatfs(int fd, struct statfs *buf);

#endif /* __SYS_MOUNT_H */
