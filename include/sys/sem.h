/* sys/sem.h - SysV semaphores (POSIX XSI) for CCCC
 *
 * struct semid_ds diverges the same way as shmid_ds/msqid_ds (see
 * sys/shm.h) -- macOS #pragma pack(4) vs glibc field order + reserved
 * padding -- so this declares a CCCC-canonical struct instead. The sem
 * command set (GETVAL/SETVAL/...) numbers differently per platform
 * (macOS 3-9, glibc 11-17), so those stay #ifdef __APPLE__-split; struct
 * sembuf is naturally-aligned and byte-identical on both hosts, so it's a
 * raw pass-through.
 *
 * union semun by value: CCCC's FFI marshalling layer treats every
 * fixed-width scalar/pointer variadic-tail argument as a plain 64-bit
 * register value (see wrap_open's va_arg(ap, unsigned int) precedent in
 * src/stdlib/posix.c), but does not support passing an aggregate by value
 * through a variadic FFI call (vector-by-value through FFI is explicitly
 * rejected at compile time; a plain struct/union isn't rejected but isn't
 * marshalled correctly either). union semun is defined here for source
 * compatibility with existing POSIX code that declares one, but pass its
 * *contents* (the raw int, or the pointer) directly as the semctl() vararg
 * rather than passing the union itself by value -- e.g.
 * "semctl(id, 0, SETVAL, 5)" or "semctl(id, 0, IPC_STAT, buf)", not
 * "semctl(id, 0, SETVAL, arg)" where arg is a union semun. Both forms
 * produce the same bit pattern in the argument register on every
 * supported host, so wrap_semctl's va_arg(ap, long long) read is correct
 * either way -- only passing the union type itself by value is unsupported.
 */

#ifndef __SYS_SEM_H
#define __SYS_SEM_H

#ifdef _WIN32
#error "<sys/sem.h> is only available on POSIX targets in CCCC"
#endif

#include "sys/types.h"
#include "sys/ipc.h"
#include "time.h" /* for time_t */

struct sembuf {
    unsigned short sem_num; /* [XSI] semaphore # */
    short sem_op;           /* [XSI] semaphore operation */
    short sem_flg;          /* [XSI] operation flags */
};

struct semid_ds {
    struct ipc_perm sem_perm; /* [XSI] operation permission struct */
    unsigned short sem_nsems; /* [XSI] number of sems in set */
    time_t sem_otime;        /* [XSI] last semop() time */
    time_t sem_ctime;        /* [XSI] last time changed by semctl() */
};

union semun {
    int val;               /* value for SETVAL */
    struct semid_ds *buf;   /* buffer for IPC_STAT & IPC_SET */
    unsigned short *array;  /* array for GETALL & SETALL */
};

#define SEM_UNDO 010000

#ifdef __APPLE__
#define GETNCNT 3
#define GETPID  4
#define GETVAL  5
#define GETALL  6
#define GETZCNT 7
#define SETVAL  8
#define SETALL  9
#else
#define GETPID  11
#define GETVAL  12
#define GETALL  13
#define GETNCNT 14
#define GETZCNT 15
#define SETVAL  16
#define SETALL  17
#endif

#define SEM_A 0200 /* alter permission */
#define SEM_R 0400 /* read permission */

#ifdef __linux__
#define SEM_STAT     18
#define SEM_INFO     19
#define SEM_STAT_ANY 20

struct seminfo {
    int semmap;
    int semmni;
    int semmns;
    int semmnu;
    int semmsl;
    int semopm;
    int semume;
    int semusz;
    int semvmx;
    int semaem;
};
#endif

extern int semget(key_t key, int nsems, int semflg);
extern int semop(int semid, struct sembuf *sops, size_t nsops);
extern int semctl(int semid, int semnum, int cmd, ...);

#endif /* __SYS_SEM_H */
