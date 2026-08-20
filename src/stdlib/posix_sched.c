// posix_sched.c -- sched.h scheduling policy translation, plus
// sysconf/pathconf/fpathconf/confstr (#946 split of posix.c).
#include "posix_util.h"

#if !defined(_WIN32) && !defined(_WIN64)

// ---------------------------------------------------------------------------
// sysconf()/pathconf()/fpathconf()/confstr() — translating wrappers (#732)
//
// include/unistd.h (the guest-visible header served to compiled programs)
// defines its own canonical, host-independent numbering for _SC_*/_PC_*/_CS_*
// so compiled .c4 bytecode stays portable across hosts whose libc disagree on
// these numbers (e.g. macOS vs glibc). This file, however, is host C compiled
// against the *real* system <unistd.h> included above, so plain `_SC_FOO`
// here already refers to the host's own numbering, not the guest's. The enum
// below re-lists the guest's canonical values under a `CCCC_SC_*`/`CCCC_PC_*`
// prefix purely so the switch statements can name them without colliding with
// the host's identically-named system macros. Keep these numbers in sync
// with include/unistd.h if either changes.
enum {
    CCCC_SC_ARG_MAX = 1,
    CCCC_SC_CHILD_MAX,
    CCCC_SC_CLK_TCK,
    CCCC_SC_NGROUPS_MAX,
    CCCC_SC_OPEN_MAX,
    CCCC_SC_STREAM_MAX,
    CCCC_SC_TZNAME_MAX,
    CCCC_SC_JOB_CONTROL,
    CCCC_SC_SAVED_IDS,
    CCCC_SC_VERSION,
    CCCC_SC_PAGESIZE,
    CCCC_SC_NPROCESSORS_CONF,
    CCCC_SC_NPROCESSORS_ONLN,
    CCCC_SC_PHYS_PAGES,
    CCCC_SC_LINE_MAX,
    CCCC_SC_RE_DUP_MAX,
    CCCC_SC_2_VERSION,
    CCCC_SC_XOPEN_VERSION,
    CCCC_SC_HOST_NAME_MAX,
    CCCC_SC_LOGIN_NAME_MAX,
    CCCC_SC_TTY_NAME_MAX,
    CCCC_SC_SYMLOOP_MAX,
    CCCC_SC_ATEXIT_MAX,
    CCCC_SC_IOV_MAX,
    CCCC_SC_GETPW_R_SIZE_MAX,
    CCCC_SC_GETGR_R_SIZE_MAX,
    CCCC_SC_MONOTONIC_CLOCK,
};

enum {
    CCCC_PC_LINK_MAX = 1,
    CCCC_PC_MAX_CANON,
    CCCC_PC_MAX_INPUT,
    CCCC_PC_NAME_MAX,
    CCCC_PC_PATH_MAX,
    CCCC_PC_PIPE_BUF,
    CCCC_PC_CHOWN_RESTRICTED,
    CCCC_PC_NO_TRUNC,
    CCCC_PC_VDISABLE,
};

enum { CCCC_CS_PATH = 1 };

// Version queries answer with CCCC's own VM-model constants rather than
// consulting the host (mirrors _POSIX_VERSION/_XOPEN_VERSION in unistd.h).
static long long wrap_sysconf(long long name) {
    switch (name) {
        case CCCC_SC_VERSION:
            return 200809L;
        case CCCC_SC_2_VERSION:
            return 200809L;
        case CCCC_SC_XOPEN_VERSION:
            return 700;
#ifdef _SC_ARG_MAX
        case CCCC_SC_ARG_MAX:
            return (long long)sysconf(_SC_ARG_MAX);
#endif
#ifdef _SC_CHILD_MAX
        case CCCC_SC_CHILD_MAX:
            return (long long)sysconf(_SC_CHILD_MAX);
#endif
#ifdef _SC_CLK_TCK
        case CCCC_SC_CLK_TCK:
            return (long long)sysconf(_SC_CLK_TCK);
#endif
#ifdef _SC_NGROUPS_MAX
        case CCCC_SC_NGROUPS_MAX:
            return (long long)sysconf(_SC_NGROUPS_MAX);
#endif
#ifdef _SC_OPEN_MAX
        case CCCC_SC_OPEN_MAX:
            return (long long)sysconf(_SC_OPEN_MAX);
#endif
#ifdef _SC_STREAM_MAX
        case CCCC_SC_STREAM_MAX:
            return (long long)sysconf(_SC_STREAM_MAX);
#endif
#ifdef _SC_TZNAME_MAX
        case CCCC_SC_TZNAME_MAX:
            return (long long)sysconf(_SC_TZNAME_MAX);
#endif
#ifdef _SC_JOB_CONTROL
        case CCCC_SC_JOB_CONTROL:
            return (long long)sysconf(_SC_JOB_CONTROL);
#endif
#ifdef _SC_SAVED_IDS
        case CCCC_SC_SAVED_IDS:
            return (long long)sysconf(_SC_SAVED_IDS);
#endif
#ifdef _SC_PAGESIZE
        case CCCC_SC_PAGESIZE:
            return (long long)sysconf(_SC_PAGESIZE);
#elif defined(_SC_PAGE_SIZE)
        case CCCC_SC_PAGESIZE:
            return (long long)sysconf(_SC_PAGE_SIZE);
#endif
#ifdef _SC_NPROCESSORS_CONF
        case CCCC_SC_NPROCESSORS_CONF:
            return (long long)sysconf(_SC_NPROCESSORS_CONF);
#endif
#ifdef _SC_NPROCESSORS_ONLN
        case CCCC_SC_NPROCESSORS_ONLN:
            return (long long)sysconf(_SC_NPROCESSORS_ONLN);
#endif
#ifdef _SC_PHYS_PAGES
        case CCCC_SC_PHYS_PAGES:
            return (long long)sysconf(_SC_PHYS_PAGES);
#endif
#ifdef _SC_LINE_MAX
        case CCCC_SC_LINE_MAX:
            return (long long)sysconf(_SC_LINE_MAX);
#endif
#ifdef _SC_RE_DUP_MAX
        case CCCC_SC_RE_DUP_MAX:
            return (long long)sysconf(_SC_RE_DUP_MAX);
#endif
#ifdef _SC_HOST_NAME_MAX
        case CCCC_SC_HOST_NAME_MAX:
            return (long long)sysconf(_SC_HOST_NAME_MAX);
#endif
#ifdef _SC_LOGIN_NAME_MAX
        case CCCC_SC_LOGIN_NAME_MAX:
            return (long long)sysconf(_SC_LOGIN_NAME_MAX);
#endif
#ifdef _SC_TTY_NAME_MAX
        case CCCC_SC_TTY_NAME_MAX:
            return (long long)sysconf(_SC_TTY_NAME_MAX);
#endif
#ifdef _SC_SYMLOOP_MAX
        case CCCC_SC_SYMLOOP_MAX:
            return (long long)sysconf(_SC_SYMLOOP_MAX);
#endif
#ifdef _SC_ATEXIT_MAX
        case CCCC_SC_ATEXIT_MAX:
            return (long long)sysconf(_SC_ATEXIT_MAX);
#endif
#ifdef _SC_IOV_MAX
        case CCCC_SC_IOV_MAX:
            return (long long)sysconf(_SC_IOV_MAX);
#endif
#ifdef _SC_GETPW_R_SIZE_MAX
        case CCCC_SC_GETPW_R_SIZE_MAX:
            return (long long)sysconf(_SC_GETPW_R_SIZE_MAX);
#endif
#ifdef _SC_GETGR_R_SIZE_MAX
        case CCCC_SC_GETGR_R_SIZE_MAX:
            return (long long)sysconf(_SC_GETGR_R_SIZE_MAX);
#endif
#ifdef _SC_MONOTONIC_CLOCK
        case CCCC_SC_MONOTONIC_CLOCK:
            return (long long)sysconf(_SC_MONOTONIC_CLOCK);
#endif
        default:
            errno = EINVAL;
            return -1;
    }
}

static long long wrap_pathconf(long long path, long long name) {
    switch (name) {
#ifdef _PC_LINK_MAX
        case CCCC_PC_LINK_MAX:
            return (long long)pathconf((const char *)path, _PC_LINK_MAX);
#endif
#ifdef _PC_MAX_CANON
        case CCCC_PC_MAX_CANON:
            return (long long)pathconf((const char *)path, _PC_MAX_CANON);
#endif
#ifdef _PC_MAX_INPUT
        case CCCC_PC_MAX_INPUT:
            return (long long)pathconf((const char *)path, _PC_MAX_INPUT);
#endif
#ifdef _PC_NAME_MAX
        case CCCC_PC_NAME_MAX:
            return (long long)pathconf((const char *)path, _PC_NAME_MAX);
#endif
#ifdef _PC_PATH_MAX
        case CCCC_PC_PATH_MAX:
            return (long long)pathconf((const char *)path, _PC_PATH_MAX);
#endif
#ifdef _PC_PIPE_BUF
        case CCCC_PC_PIPE_BUF:
            return (long long)pathconf((const char *)path, _PC_PIPE_BUF);
#endif
#ifdef _PC_CHOWN_RESTRICTED
        case CCCC_PC_CHOWN_RESTRICTED:
            return (long long)pathconf((const char *)path,
                                       _PC_CHOWN_RESTRICTED);
#endif
#ifdef _PC_NO_TRUNC
        case CCCC_PC_NO_TRUNC:
            return (long long)pathconf((const char *)path, _PC_NO_TRUNC);
#endif
#ifdef _PC_VDISABLE
        case CCCC_PC_VDISABLE:
            return (long long)pathconf((const char *)path, _PC_VDISABLE);
#endif
        default:
            errno = EINVAL;
            return -1;
    }
}

static long long wrap_fpathconf(long long fd, long long name) {
    switch (name) {
#ifdef _PC_LINK_MAX
        case CCCC_PC_LINK_MAX:
            return (long long)fpathconf((int)fd, _PC_LINK_MAX);
#endif
#ifdef _PC_MAX_CANON
        case CCCC_PC_MAX_CANON:
            return (long long)fpathconf((int)fd, _PC_MAX_CANON);
#endif
#ifdef _PC_MAX_INPUT
        case CCCC_PC_MAX_INPUT:
            return (long long)fpathconf((int)fd, _PC_MAX_INPUT);
#endif
#ifdef _PC_NAME_MAX
        case CCCC_PC_NAME_MAX:
            return (long long)fpathconf((int)fd, _PC_NAME_MAX);
#endif
#ifdef _PC_PATH_MAX
        case CCCC_PC_PATH_MAX:
            return (long long)fpathconf((int)fd, _PC_PATH_MAX);
#endif
#ifdef _PC_PIPE_BUF
        case CCCC_PC_PIPE_BUF:
            return (long long)fpathconf((int)fd, _PC_PIPE_BUF);
#endif
#ifdef _PC_CHOWN_RESTRICTED
        case CCCC_PC_CHOWN_RESTRICTED:
            return (long long)fpathconf((int)fd, _PC_CHOWN_RESTRICTED);
#endif
#ifdef _PC_NO_TRUNC
        case CCCC_PC_NO_TRUNC:
            return (long long)fpathconf((int)fd, _PC_NO_TRUNC);
#endif
#ifdef _PC_VDISABLE
        case CCCC_PC_VDISABLE:
            return (long long)fpathconf((int)fd, _PC_VDISABLE);
#endif
        default:
            errno = EINVAL;
            return -1;
    }
}

static long long wrap_confstr(long long name, long long buf, long long len) {
    switch (name) {
#ifdef _CS_PATH
        case CCCC_CS_PATH:
            return (long long)confstr(_CS_PATH, (char *)buf, (size_t)len);
#endif
        default:
            errno = EINVAL;
            return 0;
    }
}

// sched.h (#800) -- SCHED_OTHER/FIFO/RR genuinely disagree between hosts
// (macOS: 1/4/2, Linux: 0/1/2/3/5, verified against real headers), so the
// guest sees CCCC-canonical numbering (include/sched.h) and these wrappers
// translate to/from the host's real values. struct sched_param is also
// host-divergent (8 bytes on macOS incl. a libpthread-internal __opaque
// tail, 4 bytes on Linux) -- setparam/getparam marshal through a host-sized
// local rather than handing the guest pointer to the host directly.
// macOS's real <sched.h> declares only sched_yield/get_priority_min/max --
// no process-scheduling API at all -- so setparam/getparam/setscheduler/
// getscheduler/rr_get_interval are stubbed there to -1/ENOSYS, letting
// portable guest code still compile and link.
static int guest_to_host_sched_policy(int guest_policy) {
    switch (guest_policy) {
        case 0:
            return SCHED_OTHER; // guest SCHED_OTHER
        case 1:
            return SCHED_FIFO;  // guest SCHED_FIFO
        case 2:
            return SCHED_RR;    // guest SCHED_RR
#ifdef __linux__
        case 3:
            return SCHED_BATCH; // guest SCHED_BATCH
        case 5:
            return SCHED_IDLE;  // guest SCHED_IDLE
#endif
        default:
            return guest_policy;
    }
}

struct cccc_guest_sched_param {
    int sched_priority;
};

static long long wrap_sched_get_priority_min(long long policy) {
    return (long long)sched_get_priority_min(
        guest_to_host_sched_policy((int)policy));
}

static long long wrap_sched_get_priority_max(long long policy) {
    return (long long)sched_get_priority_max(
        guest_to_host_sched_policy((int)policy));
}

#ifdef __linux__
static long long wrap_sched_setparam(long long pid, long long param) {
    struct sched_param host_param = {0};
    if (param)
        host_param.sched_priority =
            ((struct cccc_guest_sched_param *)(void *)param)->sched_priority;
    return (long long)sched_setparam((pid_t)pid, &host_param);
}

static long long wrap_sched_getparam(long long pid, long long param) {
    struct sched_param host_param;
    int                rc = sched_getparam((pid_t)pid, &host_param);
    if (rc == 0 && param)
        ((struct cccc_guest_sched_param *)(void *)param)->sched_priority =
            host_param.sched_priority;
    return rc;
}

static long long wrap_sched_setscheduler(long long pid, long long policy,
                                         long long param) {
    struct sched_param host_param = {0};
    if (param)
        host_param.sched_priority =
            ((struct cccc_guest_sched_param *)(void *)param)->sched_priority;
    int rc = sched_setscheduler(
        (pid_t)pid, guest_to_host_sched_policy((int)policy), &host_param);
    return rc;
}

static int host_to_guest_sched_policy(int host_policy) {
    if (host_policy == SCHED_OTHER)
        return 0;
    if (host_policy == SCHED_FIFO)
        return 1;
    if (host_policy == SCHED_RR)
        return 2;
    if (host_policy == SCHED_BATCH)
        return 3;
    if (host_policy == SCHED_IDLE)
        return 5;
    return host_policy;
}

static long long wrap_sched_getscheduler(long long pid) {
    int rc = sched_getscheduler((pid_t)pid);
    if (rc < 0)
        return rc;
    return host_to_guest_sched_policy(rc);
}

static long long wrap_sched_rr_get_interval(long long pid, long long interval) {
    return (long long)sched_rr_get_interval((pid_t)pid,
                                            (struct timespec *)interval);
}
#else
static long long wrap_sched_setparam(long long pid, long long param) {
    (void)pid;
    (void)param;
    errno = ENOSYS;
    return -1;
}

static long long wrap_sched_getparam(long long pid, long long param) {
    (void)pid;
    (void)param;
    errno = ENOSYS;
    return -1;
}

static long long wrap_sched_setscheduler(long long pid, long long policy,
                                         long long param) {
    (void)pid;
    (void)policy;
    (void)param;
    errno = ENOSYS;
    return -1;
}

static long long wrap_sched_getscheduler(long long pid) {
    (void)pid;
    errno = ENOSYS;
    return -1;
}

static long long wrap_sched_rr_get_interval(long long pid, long long interval) {
    (void)pid;
    (void)interval;
    errno = ENOSYS;
    return -1;
}
#endif

void register_posix_sched_functions(VirtualMachine *vm) {
    cc_register_cfunc(vm, "sysconf", (void *)wrap_sysconf, 1, 0);
    cc_register_cfunc(vm, "pathconf", (void *)wrap_pathconf, 2, 0);
    cc_register_cfunc(vm, "fpathconf", (void *)wrap_fpathconf, 2, 0);
    cc_register_cfunc(vm, "confstr", (void *)wrap_confstr, 3, 0);
    cc_register_cfunc(vm, "sched_yield", (void *)sched_yield, 0, 0);
    cc_register_cfunc(vm, "sched_get_priority_min",
                      (void *)wrap_sched_get_priority_min, 1, 0);
    cc_register_cfunc(vm, "sched_get_priority_max",
                      (void *)wrap_sched_get_priority_max, 1, 0);
#ifdef __linux__
    cc_register_cfunc(vm, "sched_setparam", (void *)wrap_sched_setparam, 2, 0);
    cc_register_cfunc(vm, "sched_getparam", (void *)wrap_sched_getparam, 2, 0);
    cc_register_cfunc(vm, "sched_setscheduler", (void *)wrap_sched_setscheduler,
                      3, 0);
    cc_register_cfunc(vm, "sched_getscheduler", (void *)wrap_sched_getscheduler,
                      1, 0);
    cc_register_cfunc(vm, "sched_rr_get_interval",
                      (void *)wrap_sched_rr_get_interval, 2, 0);
#else
    // No process-scheduling API on this host at all (#824) -- only register
    // the always-ENOSYS stubs if the caller opted in via --posix-emulation;
    // matches sched.h's declaration guard.
    if (vm->flags & CCCC_POSIX_EMULATION) {
        cc_register_cfunc(vm, "sched_setparam", (void *)wrap_sched_setparam, 2,
                          0);
        cc_register_cfunc(vm, "sched_getparam", (void *)wrap_sched_getparam, 2,
                          0);
        cc_register_cfunc(vm, "sched_setscheduler",
                          (void *)wrap_sched_setscheduler, 3, 0);
        cc_register_cfunc(vm, "sched_getscheduler",
                          (void *)wrap_sched_getscheduler, 1, 0);
        cc_register_cfunc(vm, "sched_rr_get_interval",
                          (void *)wrap_sched_rr_get_interval, 2, 0);
    }
#endif
}

#else
void register_posix_sched_functions(VirtualMachine *vm) {
    (void)vm;
}
#endif
