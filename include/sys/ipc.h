/* sys/ipc.h - SysV IPC common definitions (POSIX XSI) for CCCC
 *
 * struct ipc_perm diverges between macOS and glibc (macOS wraps it in
 * #pragma pack(4), which CCCC's parser doesn't support -- only fully-packed
 * __attribute__((packed)) is; glibc's field order also differs and adds
 * reserved padding). Rather than mirror either host layout, this declares a
 * CCCC-canonical struct in POSIX field order with natural alignment;
 * wrap_shmctl/wrap_semctl/wrap_msgctl (src/stdlib/posix.c) populate a
 * host-local struct via the real *ctl(IPC_STAT) and copy field-by-field --
 * the same shape as sys/statvfs.h's struct statvfs.
 *
 * IPC_CREAT/EXCL/NOWAIT and IPC_RMID/SET/STAT already agree bit-for-bit on
 * both platforms (verified against real headers), so those need no
 * per-platform split.
 */

#ifndef __SYS_IPC_H
#define __SYS_IPC_H

#ifdef _WIN32
#error "<sys/ipc.h> is only available on POSIX targets in CCCC"
#endif

#include "sys/types.h"

struct ipc_perm {
    uid_t uid;   /* [XSI] owner's user ID */
    gid_t gid;   /* [XSI] owner's group ID */
    uid_t cuid;  /* [XSI] creator's user ID */
    gid_t cgid;  /* [XSI] creator's group ID */
    mode_t mode; /* [XSI] read/write permission */
};
/* Only the five POSIX-mandated members are exposed -- the kernel-internal
 * key/sequence-number fields (macOS's _key/_seq, glibc's __key/__seq) are
 * opaque and not meaningfully portable, so they're omitted rather than
 * given placeholder values that don't round-trip through the real kernel
 * state. */

/* Mode bits -- identical numbering on macOS and Linux. */
#define IPC_CREAT  01000
#define IPC_EXCL   02000
#define IPC_NOWAIT 04000

/* Keys */
#define IPC_PRIVATE ((key_t)0)

/* Control commands -- identical numbering on macOS and Linux. */
#define IPC_RMID 0
#define IPC_SET  1
#define IPC_STAT 2

#ifdef __linux__
#define IPC_INFO 3
#endif

extern key_t ftok(const char *path, int id);

#endif /* __SYS_IPC_H */
