/* sys/shm.h - SysV shared memory (POSIX XSI) for CCCC
 *
 * struct shmid_ds diverges hard between the two hosts (macOS: #pragma
 * pack(4), perm/segsz/lpid/cpid/nattch/atime/dtime/ctime/internal order;
 * glibc: perm/segsz/atime/dtime/ctime/cpid/lpid/nattch + reserved padding,
 * naturally aligned). This declares a CCCC-canonical struct in POSIX field
 * order instead of mirroring either host; wrap_shmctl (src/stdlib/posix.c)
 * populates a host-local struct via the real shmctl() and copies
 * field-by-field, the same shape as sys/statvfs.h's struct statvfs.
 *
 * SHM_RDONLY/SHM_RND and IPC_CREAT/EXCL/NOWAIT already agree bit-for-bit
 * (verified against real headers); SHM_REMAP and friends are Linux-only
 * kernel features with no macOS equivalent.
 */

#ifndef __SYS_SHM_H
#define __SYS_SHM_H

#ifdef _WIN32
#error "<sys/shm.h> is only available on POSIX targets in CCCC"
#endif

#include "sys/types.h"
#include "sys/ipc.h"
#include "time.h" /* for time_t */

typedef unsigned long shmatt_t;

struct shmid_ds {
    struct ipc_perm shm_perm; /* [XSI] operation permission struct */
    size_t shm_segsz;         /* [XSI] size of segment in bytes */
    pid_t shm_lpid;           /* [XSI] pid of last shmop */
    pid_t shm_cpid;           /* [XSI] pid of creator */
    shmatt_t shm_nattch;      /* [XSI] number of current attaches */
    time_t shm_atime;         /* [XSI] time of last shmat() */
    time_t shm_dtime;         /* [XSI] time of last shmdt() */
    time_t shm_ctime;         /* [XSI] time of last change by shmctl() */
};

/* Mode bits -- identical numbering on macOS and Linux. */
#define SHM_RDONLY 010000
#define SHM_RND    020000

#if defined(__APPLE__) && defined(__aarch64__)
#define SHMLBA (16 * 1024)
#else
#define SHMLBA 4096
#endif

#define SHM_R (0400) /* IPC_R */
#define SHM_W (0200) /* IPC_W */

#ifdef __linux__
/* Linux-only kernel features -- no macOS equivalent. */
#define SHM_REMAP     040000
#define SHM_EXEC      0100000
#define SHM_LOCK      11
#define SHM_UNLOCK    12
#define SHM_STAT      13
#define SHM_INFO      14
#define SHM_STAT_ANY  15
#define SHM_DEST      01000
#define SHM_LOCKED    02000
#define SHM_HUGETLB   04000
#define SHM_NORESERVE 010000

/* Populated from a host struct shminfo/shm_info by wrap_shmctl for
 * SHM_INFO/IPC_INFO -- glibc's __syscall_ulong_t is unsigned long on both
 * x86_64 and aarch64, so this needs no per-arch split. */
struct shminfo {
    unsigned long shmmax;
    unsigned long shmmin;
    unsigned long shmmni;
    unsigned long shmseg;
    unsigned long shmall;
};

struct shm_info {
    int used_ids;
    unsigned long shm_tot;
    unsigned long shm_rss;
    unsigned long shm_swp;
    unsigned long swap_attempts;
    unsigned long swap_successes;
};
#endif

extern int shmget(key_t key, size_t size, int shmflg);
extern void *shmat(int shmid, const void *shmaddr, int shmflg);
extern int shmdt(const void *shmaddr);
extern int shmctl(int shmid, int cmd, struct shmid_ds *buf);

#endif /* __SYS_SHM_H */
