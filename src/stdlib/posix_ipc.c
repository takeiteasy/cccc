// posix_ipc.c -- SysV IPC: ftok + shm/sem/msg guest struct translation
// (#946 split of posix.c).
#include "posix_util.h"

#if !defined(_WIN32) && !defined(_WIN64)

// sys/ipc.h, sys/shm.h, sys/sem.h, sys/msg.h (#812) -- ipc_perm/shmid_ds/
// semid_ds/msqid_ds diverge hard between hosts: macOS wraps all four in
// #pragma pack(4) (which CCCC's parser doesn't support -- only fully-packed
// __attribute__((packed)) is), and glibc's field order differs and adds
// reserved padding on top. Rather than mirror either host layout, the guest
// sees a CCCC-canonical struct in POSIX field order (include/sys/ipc.h,
// sys/shm.h, sys/sem.h, sys/msg.h) and these wrappers marshal field-by-field
// through a host-local struct -- the same shape as wrap_statvfs above. Field
// *names* (shm_perm/shm_segsz/shm_lpid/shm_cpid/shm_nattch/shm_atime/
// shm_dtime/shm_ctime; sem_perm/sem_nsems/sem_otime/sem_ctime; msg_perm/
// msg_qnum/msg_qbytes/msg_lspid/msg_lrpid/msg_stime/msg_rtime/msg_ctime) are
// identical on macOS and glibc even though field *order* isn't, so the copy
// helpers below read by name rather than position.
//
// IPC_SET reads the host struct via IPC_STAT first, overlays uid/gid/mode
// from the guest struct, then calls IPC_SET -- never a zero-initialized
// host struct, since macOS's shm_internal and glibc's reserved fields are
// kernel-owned and a blank struct invites a platform-specific EINVAL.
//
// semctl()'s 4th argument is `union semun` by value in the real prototype,
// but CCCC's FFI marshalling has no support for passing an aggregate by
// value through a variadic call (vector-by-value through FFI is rejected
// outright; a plain struct/union isn't rejected but isn't marshalled
// correctly either -- see include/sys/sem.h's comment). wrap_semctl reads
// its 4th argument as a plain scalar/pointer instead (matching wrap_open's
// va_arg(ap, unsigned int) precedent) and guest callers pass the raw int or
// pointer directly rather than constructing a union semun value -- the
// resulting register content is identical either way on every ABI CCCC
// targets, so this differs from POSIX only in spelling, not in wire format.
// Every call must supply that 4th argument (even a dummy 0 for cmds that
// ignore it, like GETVAL/IPC_RMID) since, unlike the real libc, there is no
// tolerance here for a variadic call with zero variadic arguments.

struct cccc_guest_ipc_perm {
    uid_t  uid;
    gid_t  gid;
    uid_t  cuid;
    gid_t  cgid;
    mode_t mode;
};

static void host_to_guest_ipc_perm(const struct ipc_perm      *host,
                                   struct cccc_guest_ipc_perm *g) {
    g->uid  = host->uid;
    g->gid  = host->gid;
    g->cuid = host->cuid;
    g->cgid = host->cgid;
    g->mode = host->mode;
}

static void guest_overlay_ipc_perm(struct ipc_perm                  *host,
                                   const struct cccc_guest_ipc_perm *g) {
    host->uid  = g->uid;
    host->gid  = g->gid;
    host->mode = (mode_t)g->mode;
    // cuid/cgid are creator identity, not settable via IPC_SET on either
    // platform -- left as the STAT'd values.
}

static long long wrap_ftok(long long path, long long id) {
    return (long long)ftok((const char *)path, (int)id);
}

// --- sys/shm.h ---------------------------------------------------------

struct cccc_guest_shmid_ds {
    struct cccc_guest_ipc_perm shm_perm;
    unsigned long              shm_segsz;
    pid_t                      shm_lpid;
    pid_t                      shm_cpid;
    unsigned long              shm_nattch;
    time_t                     shm_atime;
    time_t                     shm_dtime;
    time_t                     shm_ctime;
};

static void host_to_guest_shmid_ds(const struct shmid_ds      *host,
                                   struct cccc_guest_shmid_ds *g) {
    host_to_guest_ipc_perm(&host->shm_perm, &g->shm_perm);
    g->shm_segsz  = (unsigned long)host->shm_segsz;
    g->shm_lpid   = host->shm_lpid;
    g->shm_cpid   = host->shm_cpid;
    g->shm_nattch = (unsigned long)host->shm_nattch;
    g->shm_atime  = host->shm_atime;
    g->shm_dtime  = host->shm_dtime;
    g->shm_ctime  = host->shm_ctime;
}

static long long wrap_shmget(long long key, long long size, long long shmflg) {
    return (long long)shmget((key_t)key, (size_t)size, (int)shmflg);
}

static long long wrap_shmat(long long shmid, long long shmaddr,
                            long long shmflg) {
    void *r = shmat((int)shmid, (const void *)shmaddr, (int)shmflg);
    return (long long)(intptr_t)r;
}

static long long wrap_shmdt(long long shmaddr) {
    return (long long)shmdt((const void *)shmaddr);
}

static long long wrap_shmctl(long long shmid, long long cmd, long long buf) {
    union {
        struct shmid_ds ds;
#ifdef __linux__
        struct shminfo  info;
        struct shm_info sinfo;
#endif
    } host;
    memset(&host, 0, sizeof(host));

    if (cmd == IPC_SET) {
        if (shmctl((int)shmid, IPC_STAT, &host.ds) != 0)
            return -1;
        if (buf)
            guest_overlay_ipc_perm(
                &host.ds.shm_perm,
                &((struct cccc_guest_shmid_ds *)(void *)buf)->shm_perm);
    }

    int rc = shmctl((int)shmid, (int)cmd, &host.ds);
    if (rc == 0 && buf) {
        if (cmd == IPC_STAT
#ifdef __linux__
            || cmd == SHM_STAT || cmd == SHM_STAT_ANY
#endif
        ) {
            host_to_guest_shmid_ds(&host.ds,
                                   (struct cccc_guest_shmid_ds *)(void *)buf);
        }
#ifdef __linux__
        else if (cmd == IPC_INFO) {
            struct shminfo *g = (struct shminfo *)(void *)buf;
            g->shmmax         = host.info.shmmax;
            g->shmmin         = host.info.shmmin;
            g->shmmni         = host.info.shmmni;
            g->shmseg         = host.info.shmseg;
            g->shmall         = host.info.shmall;
        } else if (cmd == SHM_INFO) {
            struct shm_info *g = (struct shm_info *)(void *)buf;
            g->used_ids        = host.sinfo.used_ids;
            g->shm_tot         = (unsigned long)host.sinfo.shm_tot;
            g->shm_rss         = (unsigned long)host.sinfo.shm_rss;
            g->shm_swp         = (unsigned long)host.sinfo.shm_swp;
            g->swap_attempts   = (unsigned long)host.sinfo.swap_attempts;
            g->swap_successes  = (unsigned long)host.sinfo.swap_successes;
        }
#endif
    }
    return rc;
}

// --- sys/sem.h -----------------------------------------------------------

struct cccc_guest_semid_ds {
    struct cccc_guest_ipc_perm sem_perm;
    unsigned short             sem_nsems;
    time_t                     sem_otime;
    time_t                     sem_ctime;
};

static void host_to_guest_semid_ds(const struct semid_ds      *host,
                                   struct cccc_guest_semid_ds *g) {
    host_to_guest_ipc_perm(&host->sem_perm, &g->sem_perm);
    g->sem_nsems = (unsigned short)host->sem_nsems;
    g->sem_otime = host->sem_otime;
    g->sem_ctime = host->sem_ctime;
}

static long long wrap_semget(long long key, long long nsems, long long semflg) {
    return (long long)semget((key_t)key, (int)nsems, (int)semflg);
}

static long long wrap_semop(long long semid, long long sops, long long nsops) {
    VirtualMachine *vm = cccc_posix_current_vm();
    if (!vm || !vm->gil_initialized)
        return (long long)semop((int)semid, (struct sembuf *)(void *)sops,
                                (size_t)nsops);
    ExecState state;
    cccc_posix_save_and_release_gil(vm, &state);
    int r = semop((int)semid, (struct sembuf *)(void *)sops, (size_t)nsops);
    cccc_posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

static long long wrap_semctl(long long semid, long long semnum, long long cmd,
                             ...) {
    va_list ap;
    va_start(ap, cmd);
    long long arg = va_arg(ap, long long);
    va_end(ap);

    switch ((int)cmd) {
        case GETVAL:
        case GETPID:
        case GETNCNT:
        case GETZCNT:
        case IPC_RMID:
            (void)arg;
            return (long long)semctl((int)semid, (int)semnum, (int)cmd);
        case SETVAL:
            return (long long)semctl((int)semid, (int)semnum, (int)cmd,
                                     (int)arg);
        case GETALL:
        case SETALL:
            return (long long)semctl((int)semid, (int)semnum, (int)cmd,
                                     (unsigned short *)(intptr_t)arg);
        case IPC_STAT:
        case IPC_SET: {
            struct semid_ds host_buf;
            memset(&host_buf, 0, sizeof(host_buf));
            if (cmd == IPC_SET) {
                if (semctl((int)semid, (int)semnum, IPC_STAT, &host_buf) != 0)
                    return -1;
                if (arg)
                    guest_overlay_ipc_perm(
                        &host_buf.sem_perm,
                        &((struct cccc_guest_semid_ds *)(void *)(intptr_t)arg)
                             ->sem_perm);
            }
            int rc = semctl((int)semid, (int)semnum, (int)cmd, &host_buf);
            if (rc == 0 && cmd == IPC_STAT && arg)
                host_to_guest_semid_ds(
                    &host_buf,
                    (struct cccc_guest_semid_ds *)(void *)(intptr_t)arg);
            return rc;
        }
#ifdef __linux__
        case SEM_STAT:
        case SEM_STAT_ANY: {
            struct semid_ds host_buf;
            memset(&host_buf, 0, sizeof(host_buf));
            int rc = semctl((int)semid, (int)semnum, (int)cmd, &host_buf);
            if (rc >= 0 && arg)
                host_to_guest_semid_ds(
                    &host_buf,
                    (struct cccc_guest_semid_ds *)(void *)(intptr_t)arg);
            return rc;
        }
        case SEM_INFO: {
            struct seminfo host_info;
            memset(&host_info, 0, sizeof(host_info));
            int rc = semctl((int)semid, (int)semnum, (int)cmd, &host_info);
            if (rc >= 0 && arg) {
                struct seminfo *g = (struct seminfo *)(void *)(intptr_t)arg;
                *g                = host_info;
            }
            return rc;
        }
#endif
        default:
            return (long long)semctl((int)semid, (int)semnum, (int)cmd,
                                     (int)arg);
    }
}

// --- sys/msg.h -------------------------------------------------------------

struct cccc_guest_msqid_ds {
    struct cccc_guest_ipc_perm msg_perm;
    unsigned long              msg_qnum;
    unsigned long              msg_qbytes;
    pid_t                      msg_lspid;
    pid_t                      msg_lrpid;
    time_t                     msg_stime;
    time_t                     msg_rtime;
    time_t                     msg_ctime;
};

static void host_to_guest_msqid_ds(const struct msqid_ds      *host,
                                   struct cccc_guest_msqid_ds *g) {
    host_to_guest_ipc_perm(&host->msg_perm, &g->msg_perm);
    g->msg_qnum   = (unsigned long)host->msg_qnum;
    g->msg_qbytes = (unsigned long)host->msg_qbytes;
    g->msg_lspid  = host->msg_lspid;
    g->msg_lrpid  = host->msg_lrpid;
    g->msg_stime  = host->msg_stime;
    g->msg_rtime  = host->msg_rtime;
    g->msg_ctime  = host->msg_ctime;
}

static long long wrap_msgget(long long key, long long msgflg) {
    return (long long)msgget((key_t)key, (int)msgflg);
}

static long long wrap_msgsnd(long long msqid, long long msgp, long long msgsz,
                             long long msgflg) {
    VirtualMachine *vm = cccc_posix_current_vm();
    if (!vm || !vm->gil_initialized)
        return (long long)msgsnd((int)msqid, (const void *)msgp, (size_t)msgsz,
                                 (int)msgflg);
    ExecState state;
    cccc_posix_save_and_release_gil(vm, &state);
    int r = msgsnd((int)msqid, (const void *)msgp, (size_t)msgsz, (int)msgflg);
    cccc_posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

static long long wrap_msgrcv(long long msqid, long long msgp, long long msgsz,
                             long long msgtyp, long long msgflg) {
    VirtualMachine *vm = cccc_posix_current_vm();
    if (!vm || !vm->gil_initialized)
        return (long long)msgrcv((int)msqid, (void *)msgp, (size_t)msgsz,
                                 (long)msgtyp, (int)msgflg);
    ExecState state;
    cccc_posix_save_and_release_gil(vm, &state);
    ssize_t r = msgrcv((int)msqid, (void *)msgp, (size_t)msgsz, (long)msgtyp,
                       (int)msgflg);
    cccc_posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

static long long wrap_msgctl(long long msqid, long long cmd, long long buf) {
    union {
        struct msqid_ds ds;
#ifdef __linux__
        struct msginfo info;
#endif
    } host;
    memset(&host, 0, sizeof(host));

    if (cmd == IPC_SET) {
        if (msgctl((int)msqid, IPC_STAT, &host.ds) != 0)
            return -1;
        if (buf)
            guest_overlay_ipc_perm(
                &host.ds.msg_perm,
                &((struct cccc_guest_msqid_ds *)(void *)buf)->msg_perm);
    }

    int rc = msgctl((int)msqid, (int)cmd, &host.ds);
    if (rc == 0 && buf) {
        if (cmd == IPC_STAT
#ifdef __linux__
            || cmd == MSG_STAT || cmd == MSG_STAT_ANY
#endif
        ) {
            host_to_guest_msqid_ds(&host.ds,
                                   (struct cccc_guest_msqid_ds *)(void *)buf);
        }
#ifdef __linux__
        else if (cmd == IPC_INFO || cmd == MSG_INFO) {
            struct msginfo *g = (struct msginfo *)(void *)buf;
            *g                = host.info;
        }
#endif
    }
    return rc;
}

void register_posix_ipc_functions(VirtualMachine *vm) {
    // sys/ipc.h, sys/shm.h, sys/sem.h, sys/msg.h (#812)
    cc_register_cfunc(vm, "ftok", (void *)wrap_ftok, 2, 0);
    cc_register_cfunc(vm, "shmget", (void *)wrap_shmget, 3, 0);
    cc_register_cfunc(vm, "shmat", (void *)wrap_shmat, 3, 0);
    cc_register_cfunc(vm, "shmdt", (void *)wrap_shmdt, 1, 0);
    cc_register_cfunc(vm, "shmctl", (void *)wrap_shmctl, 3, 0);
    cc_register_cfunc(vm, "semget", (void *)wrap_semget, 3, 0);
    cc_register_cfunc(vm, "semop", (void *)wrap_semop, 3, 0);
    cc_register_variadic_cfunc(vm, "semctl", (void *)wrap_semctl, 3, 0);
    cc_register_cfunc(vm, "msgget", (void *)wrap_msgget, 2, 0);
    cc_register_cfunc(vm, "msgsnd", (void *)wrap_msgsnd, 4, 0);
    cc_register_cfunc(vm, "msgrcv", (void *)wrap_msgrcv, 5, 0);
    cc_register_cfunc(vm, "msgctl", (void *)wrap_msgctl, 3, 0);
}

#else
void register_posix_ipc_functions(VirtualMachine *vm) {
    (void)vm;
}
#endif
