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

// #1031: this exact file is also what a native/generated re-emission's
// replayed `#include <sys/mount.h>` resolves to (run_native_backend
// forwards -I./include straight through to the host cc, and -I paths are
// searched ahead of system directories) -- but `struct statfs` below is
// CCCC's own minimal, ~56-byte projection (src/stdlib/posix_statfs.c's
// wrap_statfs/wrap_fstatfs translate field-by-field from a host-local
// struct, not the real ABI struct, precisely because the real one is
// ~2100 bytes on macOS), not the real host layout. A real host compiler
// reading this file's projection and then linking against the real libc
// statfs()/fstatfs() would size any `sizeof(struct statfs)`-derived
// buffer far too small for what those functions actually write --
// serialize.c's own member-access suppression for from_include types
// already assumes the replayed #include reaches the real host header
// (see its `type_def_is_from_include_suppressed`/
// `type_layout_is_host_owned` comments); without this hand-off that
// assumption was false under the standard `-I./include` invocation, same
// shape as #1022's pthread.h. Guard the whole CCCC-flavored body and hand
// off to the host's own <sys/mount.h> via #include_next. __CCCC__ is
// defined unconditionally by CCCC's own preprocessor before any header is
// read, so its absence here means a genuine host compiler is reprocessing
// this file -- only possible during -c=native/-c=generated serializer
// replay.
#ifdef __CCCC__

#include "sys/types.h"

#ifndef MFSTYPENAMELEN
#define MFSTYPENAMELEN 16
#endif

struct statfs {
    unsigned int  f_bsize;  /* fundamental file system block size */
    unsigned int  f_iosize; /* optimal transfer block size */
    unsigned long f_blocks; /* total data blocks in file system */
    unsigned long f_bfree;  /* free blocks in fs */
    unsigned long f_bavail; /* free blocks avail to non-superuser */
    unsigned long f_files;  /* total file nodes in file system */
    unsigned long f_ffree;  /* free file nodes in fs */
    unsigned int  f_flags;  /* copy of mount exported flags */
};

extern int statfs(const char *path, struct statfs *buf);
extern int fstatfs(int fd, struct statfs *buf);

#else

#include_next <sys/mount.h>

#endif /* __CCCC__ */

#endif /* __SYS_MOUNT_H */
