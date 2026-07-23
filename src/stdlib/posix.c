// POSIX and dlfcn stdlib function registration
#include "../cccc.h"
#include "../internal.h"

#if !defined(_WIN32) && !defined(_WIN64)
#include <arpa/inet.h>
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <getopt.h>
#include <glob.h>
#include <grp.h>
#include <libgen.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <pwd.h>
#include <regex.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#include <utime.h>

// ---------------------------------------------------------------------------
// GIL helpers (mirrors the pattern in stdlib/pthread.c)
// These are used around blocking POSIX calls so other VM threads can run.
// ---------------------------------------------------------------------------

static VirtualMachine *current_vm(void) {
    return cccc_current_ffi_vm();
}

static void posix_save_and_release_gil(VirtualMachine *vm, ExecState *state) {
    cccc_exec_state_save(vm, state);
    cccc_gil_release(vm);
}

static void posix_acquire_and_restore_gil(VirtualMachine *vm, const ExecState *state) {
    cccc_gil_acquire(vm);
    cccc_exec_state_restore(vm, state);
}

// ---------------------------------------------------------------------------
// Blocking I/O wrappers — release the GIL while the call may block
// ---------------------------------------------------------------------------

static long long wrap_read_gil(long long fd, long long buf, long long count) {
    VirtualMachine *vm = current_vm();
    if (!vm || !vm->gil_initialized)
        return (long long)read((int)fd, (void *)buf, (size_t)count);
    ExecState state;
    posix_save_and_release_gil(vm, &state);
    ssize_t r = read((int)fd, (void *)buf, (size_t)count);
    posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

static long long wrap_write_gil(long long fd, long long buf, long long count) {
    VirtualMachine *vm = current_vm();
    if (!vm || !vm->gil_initialized)
        return (long long)write((int)fd, (const void *)buf, (size_t)count);
    ExecState state;
    posix_save_and_release_gil(vm, &state);
    ssize_t r = write((int)fd, (const void *)buf, (size_t)count);
    posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

static long long wrap_pwrite_gil(long long fd, long long buf, long long count, long long offset) {
    VirtualMachine *vm = current_vm();
    if (!vm || !vm->gil_initialized)
        return (long long)pwrite((int)fd, (const void *)buf, (size_t)count, (off_t)offset);
    ExecState state;
    posix_save_and_release_gil(vm, &state);
    ssize_t r = pwrite((int)fd, (const void *)buf, (size_t)count, (off_t)offset);
    posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

static long long wrap_nanosleep_gil(long long req, long long rem) {
    VirtualMachine *vm = current_vm();
    if (!vm || !vm->gil_initialized)
        return (long long)nanosleep((const struct timespec *)req, (struct timespec *)rem);
    ExecState state;
    posix_save_and_release_gil(vm, &state);
    int r = nanosleep((const struct timespec *)req, (struct timespec *)rem);
    posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

static long long wrap_pread_gil(long long fd, long long buf, long long count, long long offset) {
    VirtualMachine *vm = current_vm();
    if (!vm || !vm->gil_initialized)
        return (long long)pread((int)fd, (void *)buf, (size_t)count, (off_t)offset);
    ExecState state;
    posix_save_and_release_gil(vm, &state);
    ssize_t r = pread((int)fd, (void *)buf, (size_t)count, (off_t)offset);
    posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

static long long wrap_poll_gil(long long fds, long long nfds, long long timeout) {
    VirtualMachine *vm = current_vm();
    if (!vm || !vm->gil_initialized)
        return (long long)poll((struct pollfd *)fds, (nfds_t)nfds, (int)timeout);
    ExecState state;
    posix_save_and_release_gil(vm, &state);
    int r = poll((struct pollfd *)fds, (nfds_t)nfds, (int)timeout);
    posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

static long long wrap_accept_gil(long long sockfd, long long addr, long long addrlen) {
    VirtualMachine *vm = current_vm();
    if (!vm || !vm->gil_initialized)
        return (long long)accept((int)sockfd, (struct sockaddr *)addr, (socklen_t *)addrlen);
    ExecState state;
    posix_save_and_release_gil(vm, &state);
    int r = accept((int)sockfd, (struct sockaddr *)addr, (socklen_t *)addrlen);
    posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

static long long wrap_connect_gil(long long sockfd, long long addr, long long addrlen) {
    VirtualMachine *vm = current_vm();
    if (!vm || !vm->gil_initialized)
        return (long long)connect((int)sockfd, (const struct sockaddr *)addr, (socklen_t)addrlen);
    ExecState state;
    posix_save_and_release_gil(vm, &state);
    int r = connect((int)sockfd, (const struct sockaddr *)addr, (socklen_t)addrlen);
    posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

static long long wrap_recv_gil(long long sockfd, long long buf, long long len, long long flags) {
    VirtualMachine *vm = current_vm();
    if (!vm || !vm->gil_initialized)
        return (long long)recv((int)sockfd, (void *)buf, (size_t)len, (int)flags);
    ExecState state;
    posix_save_and_release_gil(vm, &state);
    ssize_t r = recv((int)sockfd, (void *)buf, (size_t)len, (int)flags);
    posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

static long long wrap_send_gil(long long sockfd, long long buf, long long len, long long flags) {
    VirtualMachine *vm = current_vm();
    if (!vm || !vm->gil_initialized)
        return (long long)send((int)sockfd, (const void *)buf, (size_t)len, (int)flags);
    ExecState state;
    posix_save_and_release_gil(vm, &state);
    ssize_t r = send((int)sockfd, (const void *)buf, (size_t)len, (int)flags);
    posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

static long long wrap_recvfrom_gil(long long sockfd, long long buf, long long len, long long flags,
                                    long long addr, long long addrlen) {
    VirtualMachine *vm = current_vm();
    if (!vm || !vm->gil_initialized)
        return (long long)recvfrom((int)sockfd, (void *)buf, (size_t)len, (int)flags,
                                    (struct sockaddr *)addr, (socklen_t *)addrlen);
    ExecState state;
    posix_save_and_release_gil(vm, &state);
    ssize_t r = recvfrom((int)sockfd, (void *)buf, (size_t)len, (int)flags,
                          (struct sockaddr *)addr, (socklen_t *)addrlen);
    posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

static long long wrap_sendto_gil(long long sockfd, long long buf, long long len, long long flags,
                                  long long addr, long long addrlen) {
    VirtualMachine *vm = current_vm();
    if (!vm || !vm->gil_initialized)
        return (long long)sendto((int)sockfd, (const void *)buf, (size_t)len, (int)flags,
                                  (const struct sockaddr *)addr, (socklen_t)addrlen);
    ExecState state;
    posix_save_and_release_gil(vm, &state);
    ssize_t r = sendto((int)sockfd, (const void *)buf, (size_t)len, (int)flags,
                        (const struct sockaddr *)addr, (socklen_t)addrlen);
    posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

static long long wrap_wait_gil(long long wstatus) {
    VirtualMachine *vm = current_vm();
    if (!vm || !vm->gil_initialized)
        return (long long)wait((int *)wstatus);
    ExecState state;
    posix_save_and_release_gil(vm, &state);
    pid_t r = wait((int *)wstatus);
    posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

static long long wrap_waitpid_gil(long long pid, long long wstatus, long long options) {
    VirtualMachine *vm = current_vm();
    if (!vm || !vm->gil_initialized)
        return (long long)waitpid((pid_t)pid, (int *)wstatus, (int)options);
    ExecState state;
    posix_save_and_release_gil(vm, &state);
    pid_t r = waitpid((pid_t)pid, (int *)wstatus, (int)options);
    posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

static long long wrap_sleep_gil(long long seconds) {
    VirtualMachine *vm = current_vm();
    if (!vm || !vm->gil_initialized)
        return (long long)sleep((unsigned int)seconds);
    ExecState state;
    posix_save_and_release_gil(vm, &state);
    unsigned int r = sleep((unsigned int)seconds);
    posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

static long long wrap_usleep_gil(long long usec) {
    VirtualMachine *vm = current_vm();
    if (!vm || !vm->gil_initialized)
        return (long long)usleep((useconds_t)usec);
    ExecState state;
    posix_save_and_release_gil(vm, &state);
    int r = usleep((useconds_t)usec);
    posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

static long long wrap_pause_gil(void) {
    VirtualMachine *vm = current_vm();
    if (!vm || !vm->gil_initialized)
        return (long long)pause();
    ExecState state;
    posix_save_and_release_gil(vm, &state);
    int r = pause();
    posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

// ---------------------------------------------------------------------------
// Non-blocking wrappers (intentionally keep the GIL — fast, no I/O wait)
// ---------------------------------------------------------------------------

static long long wrap_open(const char *path, long long oflag, ...) {
    mode_t mode = 0;
    if (oflag & O_CREAT) {
        va_list ap;
        va_start(ap, oflag);
        mode = (mode_t)(unsigned int)va_arg(ap, unsigned int);
        va_end(ap);
    }
    return (long long)open(path, (int)oflag, mode);
}

static long long wrap_creat(const char *path, long long mode) {
    return (long long)creat(path, (mode_t)mode);
}

// The following wrappers are non-blocking or return immediately; they
// intentionally hold the GIL: close, lseek, access, unlink, rmdir, chdir,
// getcwd, stat/fstat/lstat, chmod, mkdir, mkfifo, umask, pipe, fork, _exit,
// execv/execve/execl/execlp/execle/execvp (exec replaces the process),
// mmap/munmap/mprotect/msync/posix_madvise (kernel operations, not blocking),
// socket/bind/listen/shutdown/setsockopt/getsockname (non-blocking control ops),
// gethostbyname/getaddrinfo (may block — future work to add GIL release here),
// opendir/readdir/closedir, tcgetattr/tcsetattr, getpwuid/getpwnam/getgrgid/getgrnam,
// regcomp/regexec/regerror/regfree/glob/globfree (CPU-bound, no I/O wait),
// string/network byte-order helpers.

static long long wrap_close(long long fd) { return (long long)close((int)fd); }
static long long wrap_lseek(long long fd, long long offset, long long whence) { return (long long)lseek((int)fd, (off_t)offset, (int)whence); }
static long long wrap_access(long long path, long long mode) { return (long long)access((const char *)path, (int)mode); }
static long long wrap_unlink(long long path) { return (long long)unlink((const char *)path); }
static long long wrap_rmdir(long long path) { return (long long)rmdir((const char *)path); }
static long long wrap_chdir(long long path) { return (long long)chdir((const char *)path); }
static long long wrap_fork(void) { return (long long)fork(); }
static long long wrap_pipe(long long fd) { return (long long)pipe((int *)fd); }
static long long wrap__exit(long long status) { _exit((int)status); return 0; }

static long long wrap_umask(long long cmask) { return (long long)umask((mode_t)cmask); }
static long long wrap_htonl(long long hostlong) { return (long long)htonl((uint32_t)hostlong); }
static long long wrap_htons(long long hostshort) { return (long long)htons((uint16_t)hostshort); }
static long long wrap_ntohl(long long netlong) { return (long long)ntohl((uint32_t)netlong); }
static long long wrap_ntohs(long long netshort) { return (long long)ntohs((uint16_t)netshort); }
static long long wrap_inet_addr(long long cp) { return (long long)inet_addr((const char *)cp); }
static long long wrap_basename(long long path) { return (long long)basename((char *)path); }
static long long wrap_dirname(long long path) { return (long long)dirname((char *)path); }
static long long wrap_bzero(long long s, long long n) { bzero((void *)s, (size_t)n); return 0; }
static long long wrap_bcopy(long long src, long long dst, long long n) { bcopy((const void *)src, (void *)dst, (size_t)n); return 0; }
static long long wrap_freeaddrinfo(long long res) { freeaddrinfo((struct addrinfo *)res); return 0; }
static long long wrap_globfree(long long pglob) { globfree((glob_t *)pglob); return 0; }
static long long wrap_regfree(long long preg) { regfree((regex_t *)preg); return 0; }

// ---------------------------------------------------------------------------
// glob()'s errfunc / scandir()'s select+compar callbacks (#738)
//
// Same shape as qsort/bsearch's comparator fix above: a thread-local slot
// holds which guest callback to invoke, a real host C trampoline reads it
// and drives cccc_call_guest_callback, and the slot is saved/restored around
// the host call (not written once) so a guest callback that itself calls
// glob()/scandir() doesn't clobber the outer call's slot. Faults are latched
// and surfaced via errno after the host call returns, since none of these
// host APIs give the callback its own error-propagation channel.
// ---------------------------------------------------------------------------

static _Thread_local long long g_glob_errfunc_value;
static _Thread_local int g_glob_errfunc_faulted;

static int glob_errfunc_trampoline(const char *epath, int eerrno) {
    VirtualMachine *vm = cccc_current_ffi_vm();
    long long args[2] = { (long long)(intptr_t)epath, (long long)eerrno };
    long long result = 0;
    if (!vm || cccc_call_guest_callback(vm, g_glob_errfunc_value, args, 2, &result) != 0) {
        g_glob_errfunc_faulted = 1;
        return 0; /* keep glob() enumerating rather than abort on a faulting errfunc */
    }
    return (int)result;
}

static long long wrap_glob(long long pattern, long long flags, long long errfunc, long long pglob) {
    long long saved_errfunc = g_glob_errfunc_value;
    int saved_faulted = g_glob_errfunc_faulted;
    g_glob_errfunc_value = errfunc;
    g_glob_errfunc_faulted = 0;

    int r = glob((const char *)pattern, (int)flags,
                errfunc ? glob_errfunc_trampoline : NULL, (glob_t *)pglob);

    int faulted = g_glob_errfunc_faulted;
    g_glob_errfunc_value = saved_errfunc;
    g_glob_errfunc_faulted = saved_faulted;
    if (faulted)
        errno = EFAULT;
    return r;
}

static _Thread_local long long g_scandir_select_value;
static _Thread_local long long g_scandir_compar_value;
static _Thread_local int g_scandir_faulted;

static int scandir_select_trampoline(const struct dirent *e) {
    VirtualMachine *vm = cccc_current_ffi_vm();
    long long args[1] = { (long long)(intptr_t)e };
    long long result = 0;
    if (!vm || cccc_call_guest_callback(vm, g_scandir_select_value, args, 1, &result) != 0) {
        g_scandir_faulted = 1;
        return 0;
    }
    return (int)result;
}

static int scandir_compar_trampoline(const struct dirent **a, const struct dirent **b) {
    VirtualMachine *vm = cccc_current_ffi_vm();
    long long args[2] = { (long long)(intptr_t)a, (long long)(intptr_t)b };
    long long result = 0;
    if (!vm || cccc_call_guest_callback(vm, g_scandir_compar_value, args, 2, &result) != 0) {
        g_scandir_faulted = 1;
        return 0;
    }
    return (int)result;
}

static long long wrap_scandir(long long dirname, long long namelist, long long select, long long compar) {
    long long saved_select = g_scandir_select_value;
    long long saved_compar = g_scandir_compar_value;
    int saved_faulted = g_scandir_faulted;
    g_scandir_select_value = select;
    g_scandir_compar_value = compar;
    g_scandir_faulted = 0;

    int n = scandir((const char *)dirname, (struct dirent ***)namelist,
                    select ? scandir_select_trampoline : NULL,
                    compar ? scandir_compar_trampoline : NULL);

    int faulted = g_scandir_faulted;
    g_scandir_select_value = saved_select;
    g_scandir_compar_value = saved_compar;
    g_scandir_faulted = saved_faulted;
    if (faulted)
        errno = EFAULT;
    return n;
}

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
    CCCC_SC_ARG_MAX = 1, CCCC_SC_CHILD_MAX, CCCC_SC_CLK_TCK,
    CCCC_SC_NGROUPS_MAX, CCCC_SC_OPEN_MAX, CCCC_SC_STREAM_MAX,
    CCCC_SC_TZNAME_MAX, CCCC_SC_JOB_CONTROL, CCCC_SC_SAVED_IDS,
    CCCC_SC_VERSION, CCCC_SC_PAGESIZE, CCCC_SC_NPROCESSORS_CONF,
    CCCC_SC_NPROCESSORS_ONLN, CCCC_SC_PHYS_PAGES, CCCC_SC_LINE_MAX,
    CCCC_SC_RE_DUP_MAX, CCCC_SC_2_VERSION, CCCC_SC_XOPEN_VERSION,
    CCCC_SC_HOST_NAME_MAX, CCCC_SC_LOGIN_NAME_MAX, CCCC_SC_TTY_NAME_MAX,
    CCCC_SC_SYMLOOP_MAX, CCCC_SC_ATEXIT_MAX, CCCC_SC_IOV_MAX,
    CCCC_SC_GETPW_R_SIZE_MAX, CCCC_SC_GETGR_R_SIZE_MAX,
    CCCC_SC_MONOTONIC_CLOCK,
};

enum {
    CCCC_PC_LINK_MAX = 1, CCCC_PC_MAX_CANON, CCCC_PC_MAX_INPUT,
    CCCC_PC_NAME_MAX, CCCC_PC_PATH_MAX, CCCC_PC_PIPE_BUF,
    CCCC_PC_CHOWN_RESTRICTED, CCCC_PC_NO_TRUNC, CCCC_PC_VDISABLE,
};

enum { CCCC_CS_PATH = 1 };

// Version queries answer with CCCC's own VM-model constants rather than
// consulting the host (mirrors _POSIX_VERSION/_XOPEN_VERSION in unistd.h).
static long long wrap_sysconf(long long name) {
    switch (name) {
    case CCCC_SC_VERSION:       return 200809L;
    case CCCC_SC_2_VERSION:     return 200809L;
    case CCCC_SC_XOPEN_VERSION: return 700;
#ifdef _SC_ARG_MAX
    case CCCC_SC_ARG_MAX:       return (long long)sysconf(_SC_ARG_MAX);
#endif
#ifdef _SC_CHILD_MAX
    case CCCC_SC_CHILD_MAX:     return (long long)sysconf(_SC_CHILD_MAX);
#endif
#ifdef _SC_CLK_TCK
    case CCCC_SC_CLK_TCK:       return (long long)sysconf(_SC_CLK_TCK);
#endif
#ifdef _SC_NGROUPS_MAX
    case CCCC_SC_NGROUPS_MAX:   return (long long)sysconf(_SC_NGROUPS_MAX);
#endif
#ifdef _SC_OPEN_MAX
    case CCCC_SC_OPEN_MAX:      return (long long)sysconf(_SC_OPEN_MAX);
#endif
#ifdef _SC_STREAM_MAX
    case CCCC_SC_STREAM_MAX:    return (long long)sysconf(_SC_STREAM_MAX);
#endif
#ifdef _SC_TZNAME_MAX
    case CCCC_SC_TZNAME_MAX:    return (long long)sysconf(_SC_TZNAME_MAX);
#endif
#ifdef _SC_JOB_CONTROL
    case CCCC_SC_JOB_CONTROL:   return (long long)sysconf(_SC_JOB_CONTROL);
#endif
#ifdef _SC_SAVED_IDS
    case CCCC_SC_SAVED_IDS:     return (long long)sysconf(_SC_SAVED_IDS);
#endif
#ifdef _SC_PAGESIZE
    case CCCC_SC_PAGESIZE:      return (long long)sysconf(_SC_PAGESIZE);
#elif defined(_SC_PAGE_SIZE)
    case CCCC_SC_PAGESIZE:      return (long long)sysconf(_SC_PAGE_SIZE);
#endif
#ifdef _SC_NPROCESSORS_CONF
    case CCCC_SC_NPROCESSORS_CONF: return (long long)sysconf(_SC_NPROCESSORS_CONF);
#endif
#ifdef _SC_NPROCESSORS_ONLN
    case CCCC_SC_NPROCESSORS_ONLN: return (long long)sysconf(_SC_NPROCESSORS_ONLN);
#endif
#ifdef _SC_PHYS_PAGES
    case CCCC_SC_PHYS_PAGES:    return (long long)sysconf(_SC_PHYS_PAGES);
#endif
#ifdef _SC_LINE_MAX
    case CCCC_SC_LINE_MAX:      return (long long)sysconf(_SC_LINE_MAX);
#endif
#ifdef _SC_RE_DUP_MAX
    case CCCC_SC_RE_DUP_MAX:    return (long long)sysconf(_SC_RE_DUP_MAX);
#endif
#ifdef _SC_HOST_NAME_MAX
    case CCCC_SC_HOST_NAME_MAX: return (long long)sysconf(_SC_HOST_NAME_MAX);
#endif
#ifdef _SC_LOGIN_NAME_MAX
    case CCCC_SC_LOGIN_NAME_MAX: return (long long)sysconf(_SC_LOGIN_NAME_MAX);
#endif
#ifdef _SC_TTY_NAME_MAX
    case CCCC_SC_TTY_NAME_MAX:  return (long long)sysconf(_SC_TTY_NAME_MAX);
#endif
#ifdef _SC_SYMLOOP_MAX
    case CCCC_SC_SYMLOOP_MAX:   return (long long)sysconf(_SC_SYMLOOP_MAX);
#endif
#ifdef _SC_ATEXIT_MAX
    case CCCC_SC_ATEXIT_MAX:    return (long long)sysconf(_SC_ATEXIT_MAX);
#endif
#ifdef _SC_IOV_MAX
    case CCCC_SC_IOV_MAX:       return (long long)sysconf(_SC_IOV_MAX);
#endif
#ifdef _SC_GETPW_R_SIZE_MAX
    case CCCC_SC_GETPW_R_SIZE_MAX: return (long long)sysconf(_SC_GETPW_R_SIZE_MAX);
#endif
#ifdef _SC_GETGR_R_SIZE_MAX
    case CCCC_SC_GETGR_R_SIZE_MAX: return (long long)sysconf(_SC_GETGR_R_SIZE_MAX);
#endif
#ifdef _SC_MONOTONIC_CLOCK
    case CCCC_SC_MONOTONIC_CLOCK: return (long long)sysconf(_SC_MONOTONIC_CLOCK);
#endif
    default:
        errno = EINVAL;
        return -1;
    }
}

static long long wrap_pathconf(long long path, long long name) {
    switch (name) {
#ifdef _PC_LINK_MAX
    case CCCC_PC_LINK_MAX:     return (long long)pathconf((const char *)path, _PC_LINK_MAX);
#endif
#ifdef _PC_MAX_CANON
    case CCCC_PC_MAX_CANON:    return (long long)pathconf((const char *)path, _PC_MAX_CANON);
#endif
#ifdef _PC_MAX_INPUT
    case CCCC_PC_MAX_INPUT:    return (long long)pathconf((const char *)path, _PC_MAX_INPUT);
#endif
#ifdef _PC_NAME_MAX
    case CCCC_PC_NAME_MAX:     return (long long)pathconf((const char *)path, _PC_NAME_MAX);
#endif
#ifdef _PC_PATH_MAX
    case CCCC_PC_PATH_MAX:     return (long long)pathconf((const char *)path, _PC_PATH_MAX);
#endif
#ifdef _PC_PIPE_BUF
    case CCCC_PC_PIPE_BUF:     return (long long)pathconf((const char *)path, _PC_PIPE_BUF);
#endif
#ifdef _PC_CHOWN_RESTRICTED
    case CCCC_PC_CHOWN_RESTRICTED: return (long long)pathconf((const char *)path, _PC_CHOWN_RESTRICTED);
#endif
#ifdef _PC_NO_TRUNC
    case CCCC_PC_NO_TRUNC:     return (long long)pathconf((const char *)path, _PC_NO_TRUNC);
#endif
#ifdef _PC_VDISABLE
    case CCCC_PC_VDISABLE:     return (long long)pathconf((const char *)path, _PC_VDISABLE);
#endif
    default:
        errno = EINVAL;
        return -1;
    }
}

static long long wrap_fpathconf(long long fd, long long name) {
    switch (name) {
#ifdef _PC_LINK_MAX
    case CCCC_PC_LINK_MAX:     return (long long)fpathconf((int)fd, _PC_LINK_MAX);
#endif
#ifdef _PC_MAX_CANON
    case CCCC_PC_MAX_CANON:    return (long long)fpathconf((int)fd, _PC_MAX_CANON);
#endif
#ifdef _PC_MAX_INPUT
    case CCCC_PC_MAX_INPUT:    return (long long)fpathconf((int)fd, _PC_MAX_INPUT);
#endif
#ifdef _PC_NAME_MAX
    case CCCC_PC_NAME_MAX:     return (long long)fpathconf((int)fd, _PC_NAME_MAX);
#endif
#ifdef _PC_PATH_MAX
    case CCCC_PC_PATH_MAX:     return (long long)fpathconf((int)fd, _PC_PATH_MAX);
#endif
#ifdef _PC_PIPE_BUF
    case CCCC_PC_PIPE_BUF:     return (long long)fpathconf((int)fd, _PC_PIPE_BUF);
#endif
#ifdef _PC_CHOWN_RESTRICTED
    case CCCC_PC_CHOWN_RESTRICTED: return (long long)fpathconf((int)fd, _PC_CHOWN_RESTRICTED);
#endif
#ifdef _PC_NO_TRUNC
    case CCCC_PC_NO_TRUNC:     return (long long)fpathconf((int)fd, _PC_NO_TRUNC);
#endif
#ifdef _PC_VDISABLE
    case CCCC_PC_VDISABLE:     return (long long)fpathconf((int)fd, _PC_VDISABLE);
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

// ---------------------------------------------------------------------------
// Host-global accessors (#736)
//
// errno and getopt's optarg/optind/opterr/optopt are declared in
// include/errno.h / include/getopt.h as macros expanding to
// `(*__cccc_..._ptr())` rather than plain externs, mirroring the
// stdin/stdout/stderr pattern in stdio.h. This makes them alias the host's
// real per-thread/process storage directly instead of being an inert,
// always-zero slot in the VM's own data segment (the previous behavior for
// any extern-declared-but-never-defined global). Thread-locality of errno is
// preserved for free: the same host OS thread that made the failing call
// also executes the guest code that reads it back, since VM bytecode
// execution never migrates mid-call to a different host thread.
static int *__cccc_errno_ptr(void) { return &errno; }
static char **__cccc_optarg_ptr(void) { return &optarg; }
static int *__cccc_optind_ptr(void) { return &optind; }
static int *__cccc_opterr_ptr(void) { return &opterr; }
static int *__cccc_optopt_ptr(void) { return &optopt; }

void register_posix_functions(VirtualMachine *vm) {
    // Blocking I/O — GIL released while blocked so other VM threads can run
    cc_register_cfunc(vm, "read",    (void*)wrap_read_gil,    3, 0);
    cc_register_cfunc(vm, "write",   (void*)wrap_write_gil,   3, 0);
    cc_register_cfunc(vm, "pread",   (void*)wrap_pread_gil,   4, 0);
    cc_register_cfunc(vm, "pwrite",  (void*)wrap_pwrite_gil,  4, 0);
    cc_register_cfunc(vm, "poll",    (void*)wrap_poll_gil,    3, 0);
    cc_register_cfunc(vm, "accept",  (void*)wrap_accept_gil,  3, 0);
    cc_register_cfunc(vm, "connect", (void*)wrap_connect_gil, 3, 0);
    cc_register_cfunc(vm, "recv",     (void*)wrap_recv_gil,     4, 0);
    cc_register_cfunc(vm, "send",     (void*)wrap_send_gil,     4, 0);
    cc_register_cfunc(vm, "recvfrom", (void*)wrap_recvfrom_gil, 6, 0);
    cc_register_cfunc(vm, "sendto",   (void*)wrap_sendto_gil,   6, 0);
    cc_register_cfunc(vm, "wait",    (void*)wrap_wait_gil,    1, 0);
    cc_register_cfunc(vm, "waitpid", (void*)wrap_waitpid_gil, 3, 0);
    cc_register_cfunc(vm, "sleep",   (void*)wrap_sleep_gil,   1, 0);
    cc_register_cfunc(vm, "usleep",  (void*)wrap_usleep_gil,  1, 0);
    cc_register_cfunc(vm, "nanosleep",(void*)wrap_nanosleep_gil, 2, 0);
    cc_register_cfunc(vm, "pause",   (void*)wrap_pause_gil,   0, 0);

    // Non-blocking / fast — intentionally keep the GIL (see comment above)
    cc_register_cfunc(vm, "close",   (void*)wrap_close,  1, 0);
    cc_register_cfunc(vm, "lseek",   (void*)wrap_lseek,  3, 0);
    cc_register_cfunc(vm, "access",  (void*)wrap_access, 2, 0);
    cc_register_cfunc(vm, "unlink",  (void*)wrap_unlink, 1, 0);
    cc_register_cfunc(vm, "rmdir",   (void*)wrap_rmdir,  1, 0);
    cc_register_cfunc(vm, "chdir",   (void*)wrap_chdir,  1, 0);
    cc_register_cfunc(vm, "getcwd",  (void*)getcwd, 2, 0);
    cc_register_cfunc(vm, "getpid",  (void*)getpid, 0, 0);
    cc_register_cfunc(vm, "getppid", (void*)getppid, 0, 0);
    cc_register_cfunc(vm, "fork",    (void*)wrap_fork,  0, 0);
    cc_register_cfunc(vm, "pipe",    (void*)wrap_pipe,  1, 0);
    cc_register_cfunc(vm, "_exit",   (void*)wrap__exit, 1, 0);
    cc_register_cfunc(vm, "execv",   (void*)execv,  2, 0);
    cc_register_cfunc(vm, "execve",  (void*)execve, 3, 0);
    cc_register_variadic_cfunc(vm, "execl",  (void*)execl,  2, 0);
    cc_register_variadic_cfunc(vm, "execlp", (void*)execlp, 2, 0);
    cc_register_variadic_cfunc(vm, "execle", (void*)execle, 2, 0);
    cc_register_cfunc(vm, "execvp",  (void*)execvp, 2, 0);
    cc_register_cfunc(vm, "isatty",  (void*)isatty,  1, 0);
    cc_register_cfunc(vm, "ttyname", (void*)ttyname, 1, 0);
    cc_register_cfunc(vm, "dup",     (void*)dup,     1, 0);
    cc_register_cfunc(vm, "dup2",    (void*)dup2,    2, 0);
    cc_register_cfunc(vm, "fsync",   (void*)fsync,   1, 0);
    cc_register_cfunc(vm, "ftruncate",(void*)ftruncate,2, 0);
    cc_register_cfunc(vm, "truncate", (void*)truncate, 2, 0);
    cc_register_cfunc(vm, "sysconf",   (void*)wrap_sysconf,   1, 0);
    cc_register_cfunc(vm, "pathconf",  (void*)wrap_pathconf,  2, 0);
    cc_register_cfunc(vm, "fpathconf", (void*)wrap_fpathconf, 2, 0);
    cc_register_cfunc(vm, "confstr",   (void*)wrap_confstr,   3, 0);
    cc_register_cfunc(vm, "mkstemp",  (void*)mkstemp,  1, 0);
    cc_register_cfunc(vm, "mkdtemp",  (void*)mkdtemp,  1, 0);
    cc_register_cfunc(vm, "seteuid",  (void*)seteuid,  1, 0);
    cc_register_cfunc(vm, "setegid",  (void*)setegid,  1, 0);
    cc_register_cfunc(vm, "setuid",   (void*)setuid,   1, 0);
    cc_register_cfunc(vm, "setgid",   (void*)setgid,   1, 0);
    cc_register_cfunc(vm, "getgroups",(void*)getgroups,2, 0);
    cc_register_cfunc(vm, "getlogin", (void*)getlogin, 0, 0);
    cc_register_cfunc(vm, "link",     (void*)link,     2, 0);
    cc_register_cfunc(vm, "getpgid",  (void*)getpgid,  1, 0);
    cc_register_cfunc(vm, "setpgid",  (void*)setpgid,  2, 0);
    cc_register_cfunc(vm, "getpgrp",  (void*)getpgrp,  0, 0);
    cc_register_cfunc(vm, "setsid",   (void*)setsid,   0, 0);
    cc_register_cfunc(vm, "getsid",   (void*)getsid,   1, 0);
    cc_register_cfunc(vm, "alarm",    (void*)alarm,    1, 0);
    cc_register_cfunc(vm, "fchdir",   (void*)fchdir,   1, 0);
    cc_register_cfunc(vm, "gethostname",(void*)gethostname,2, 0);
    cc_register_cfunc(vm, "sethostname",(void*)sethostname,2, 0);
    cc_register_cfunc(vm, "lchown",   (void*)lchown,   3, 0);
    cc_register_cfunc(vm, "ttyname_r",(void*)ttyname_r,3, 0);
    cc_register_cfunc(vm, "getlogin_r",(void*)getlogin_r,2, 0);
    cc_register_cfunc(vm, "setgroups",(void*)setgroups,2, 0);
    cc_register_cfunc(vm, "initgroups",(void*)initgroups,2, 0);
    cc_register_cfunc(vm, "nice",     (void*)nice,     1, 0);
    cc_register_cfunc(vm, "readv",    (void*)readv,    3, 0);
    cc_register_cfunc(vm, "writev",   (void*)writev,   3, 0);
    cc_register_variadic_cfunc(vm, "open",   (void*)wrap_open,  2, 0);
    cc_register_cfunc(vm, "creat",   (void*)wrap_creat, 2, 0);
    cc_register_variadic_cfunc(vm, "fcntl",  (void*)fcntl, 2, 0);

    cc_register_cfunc(vm, "strcasecmp",  (void*)strcasecmp,  2, 0);
    cc_register_cfunc(vm, "strncasecmp", (void*)strncasecmp, 3, 0);
    cc_register_cfunc(vm, "bcmp",   (void*)bcmp,   3, 0);
    cc_register_cfunc(vm, "index",  (void*)index,  2, 0);
    cc_register_cfunc(vm, "rindex", (void*)rindex, 2, 0);
    cc_register_cfunc(vm, "basename", (void*)wrap_basename, 1, 0);
    cc_register_cfunc(vm, "dirname",  (void*)wrap_dirname,  1, 0);
    cc_register_cfunc(vm, "fnmatch",  (void*)fnmatch, 3, 0);
    cc_register_cfunc(vm, "getopt",      (void*)getopt,      3, 0);
    cc_register_cfunc(vm, "getopt_long", (void*)getopt_long, 5, 0);
    cc_register_cfunc(vm, "__cccc_optarg_ptr", (void*)__cccc_optarg_ptr, 0, 0);
    cc_register_cfunc(vm, "__cccc_optind_ptr", (void*)__cccc_optind_ptr, 0, 0);
    cc_register_cfunc(vm, "__cccc_opterr_ptr", (void*)__cccc_opterr_ptr, 0, 0);
    cc_register_cfunc(vm, "__cccc_optopt_ptr", (void*)__cccc_optopt_ptr, 0, 0);
    cc_register_cfunc(vm, "__cccc_errno_ptr",  (void*)__cccc_errno_ptr,  0, 0);
    cc_register_cfunc(vm, "gettimeofday", (void*)gettimeofday, 2, 0);
    cc_register_cfunc(vm, "settimeofday", (void*)settimeofday, 2, 0);
    cc_register_cfunc(vm, "mmap",         (void*)mmap,         6, 0);
    cc_register_cfunc(vm, "munmap",       (void*)munmap,       2, 0);
    cc_register_cfunc(vm, "mprotect",     (void*)mprotect,     3, 0);
    cc_register_cfunc(vm, "msync",        (void*)msync,        3, 0);
    cc_register_cfunc(vm, "posix_madvise",(void*)posix_madvise,3, 0);
    cc_register_cfunc(vm, "mlock",     (void*)mlock,     2, 0);
    cc_register_cfunc(vm, "munlock",   (void*)munlock,   2, 0);
    cc_register_cfunc(vm, "mlockall",  (void*)mlockall,  1, 0);
    cc_register_cfunc(vm, "munlockall",(void*)munlockall,0, 0);
    cc_register_cfunc(vm, "shm_open",  (void*)shm_open,  3, 0);
    cc_register_cfunc(vm, "shm_unlink",(void*)shm_unlink,1, 0);
#ifdef __linux__
    // mremap: Linux-only glibc/syscall extension for resizing an existing
    // mapping. Forward-declared here (rather than defining _GNU_SOURCE
    // globally) because the host <sys/mman.h> only exposes this prototype
    // under _GNU_SOURCE; glibc still exports the symbol regardless. Needed
    // so cccc-compiled Linux code (e.g. SQLite's unix VFS syscall table)
    // that calls mremap can link (#729).
    extern void *mremap(void *old_address, size_t old_size, size_t new_size, int flags, ...);
    cc_register_cfunc(vm, "mremap", (void*)mremap, 4, 0);
    // fallocate/splice: same gap class as mremap above -- Linux-only libc
    // calls SQLite's unix VFS can reference (behind HAVE_FALLOCATE config,
    // not currently active in the smoke build) that were previously
    // undeclared and unregistered anywhere in include/ or src/stdlib/ (#731).
    extern int fallocate(int fd, int mode, off_t offset, off_t len);
    cc_register_cfunc(vm, "fallocate", (void*)fallocate, 4, 0);
    extern ssize_t splice(int fd_in, off_t *off_in, int fd_out, off_t *off_out,
                          size_t len, unsigned int flags);
    cc_register_cfunc(vm, "splice", (void*)splice, 6, 0);
#endif
    cc_register_cfunc(vm, "stat",    (void*)stat,    2, 0);
    cc_register_cfunc(vm, "fstat",   (void*)fstat,   2, 0);
    cc_register_cfunc(vm, "lstat",   (void*)lstat,   2, 0);
    cc_register_cfunc(vm, "fstatat", (void*)fstatat, 4, 0);
    cc_register_cfunc(vm, "chmod",   (void*)chmod,   2, 0);
    cc_register_cfunc(vm, "fchmod",  (void*)fchmod,  2, 0);
    cc_register_cfunc(vm, "fchmodat",(void*)fchmodat,4, 0);
    cc_register_cfunc(vm, "fchown",  (void*)fchown,  3, 0);
    cc_register_cfunc(vm, "geteuid", (void*)geteuid, 0, 0);
    cc_register_cfunc(vm, "getuid",  (void*)getuid,  0, 0);
    cc_register_cfunc(vm, "getgid",  (void*)getgid,  0, 0);
    cc_register_cfunc(vm, "getegid", (void*)getegid, 0, 0);
    cc_register_cfunc(vm, "readlink",(void*)readlink,3, 0);
    cc_register_cfunc(vm, "getpagesize",(void*)getpagesize,0, 0);
    cc_register_cfunc(vm, "mkdir",   (void*)mkdir,   2, 0);
    cc_register_cfunc(vm, "mkdirat", (void*)mkdirat, 3, 0);
    cc_register_cfunc(vm, "mkfifo",  (void*)mkfifo,  2, 0);
    cc_register_cfunc(vm, "mknod",   (void*)mknod,   3, 0);
    cc_register_cfunc(vm, "umask",   (void*)wrap_umask,  1, 0);
    cc_register_cfunc(vm, "utime",   (void*)utime,       2, 0);
    cc_register_cfunc(vm, "utimes",  (void*)utimes,      2, 0);
    cc_register_cfunc(vm, "futimes", (void*)futimes,     2, 0);
    cc_register_cfunc(vm, "lutimes", (void*)lutimes,     2, 0);
    cc_register_cfunc(vm, "setitimer",(void*)setitimer,  3, 0);
    cc_register_cfunc(vm, "getitimer",(void*)getitimer,  2, 0);
    cc_register_cfunc(vm, "htonl",   (void*)wrap_htonl,  1, 0);
    cc_register_cfunc(vm, "htons",   (void*)wrap_htons,  1, 0);
    cc_register_cfunc(vm, "ntohl",   (void*)wrap_ntohl,  1, 0);
    cc_register_cfunc(vm, "ntohs",   (void*)wrap_ntohs,  1, 0);
    cc_register_cfunc(vm, "inet_addr",(void*)wrap_inet_addr, 1, 0);
    cc_register_cfunc(vm, "inet_ntoa",(void*)inet_ntoa,    1, 0);
    cc_register_cfunc(vm, "inet_ntop",(void*)inet_ntop,    4, 0);
    cc_register_cfunc(vm, "inet_pton",(void*)inet_pton,    3, 0);
    cc_register_cfunc(vm, "bzero",   (void*)wrap_bzero, 2, 0);
    cc_register_cfunc(vm, "bcopy",   (void*)wrap_bcopy, 3, 0);

    cc_register_cfunc(vm, "socket",      (void*)socket,      3, 0);
    cc_register_cfunc(vm, "socketpair",  (void*)socketpair,  4, 0);
    cc_register_cfunc(vm, "bind",        (void*)bind,        3, 0);
    cc_register_cfunc(vm, "listen",      (void*)listen,      2, 0);
    cc_register_cfunc(vm, "setsockopt",  (void*)setsockopt,  5, 0);
    cc_register_cfunc(vm, "getsockopt",  (void*)getsockopt,  5, 0);
    cc_register_cfunc(vm, "getsockname", (void*)getsockname, 3, 0);
    cc_register_cfunc(vm, "getpeername", (void*)getpeername, 3, 0);
    cc_register_cfunc(vm, "sockatmark",  (void*)sockatmark,  1, 0);
    cc_register_cfunc(vm, "shutdown",    (void*)shutdown,    2, 0);
    cc_register_cfunc(vm, "gethostbyname",(void*)gethostbyname, 1, 0);
    cc_register_cfunc(vm, "gethostbyaddr",(void*)gethostbyaddr, 3, 0);
    cc_register_cfunc(vm, "getaddrinfo", (void*)getaddrinfo,    4, 0);
    cc_register_cfunc(vm, "freeaddrinfo",(void*)wrap_freeaddrinfo, 1, 0);
    cc_register_cfunc(vm, "getnameinfo", (void*)getnameinfo,    7, 0);

    cc_register_cfunc(vm, "opendir",  (void*)opendir,  1, 0);
    cc_register_cfunc(vm, "readdir",  (void*)readdir,  1, 0);
    cc_register_cfunc(vm, "readdir_r",(void*)readdir_r,3, 0);
    cc_register_cfunc(vm, "closedir", (void*)closedir, 1, 0);
    cc_register_cfunc(vm, "alphasort",(void*)alphasort,2, 0);
    cc_register_cfunc(vm, "tcgetattr",(void*)tcgetattr, 2, 0);
    cc_register_cfunc(vm, "tcsetattr",(void*)tcsetattr, 3, 0);
    cc_register_cfunc(vm, "cfgetispeed",(void*)cfgetispeed, 1, 0);
    cc_register_cfunc(vm, "cfgetospeed",(void*)cfgetospeed, 1, 0);
    cc_register_cfunc(vm, "cfsetispeed",(void*)cfsetispeed, 2, 0);
    cc_register_cfunc(vm, "cfsetospeed",(void*)cfsetospeed, 2, 0);
    cc_register_cfunc(vm, "cfsetspeed", (void*)cfsetspeed,  2, 0);
    cc_register_cfunc(vm, "cfmakeraw",  (void*)cfmakeraw,   1, 0);
    cc_register_cfunc(vm, "tcdrain",    (void*)tcdrain,     1, 0);
    cc_register_cfunc(vm, "tcflow",     (void*)tcflow,      2, 0);
    cc_register_cfunc(vm, "tcflush",    (void*)tcflush,     2, 0);
    cc_register_cfunc(vm, "tcsendbreak",(void*)tcsendbreak, 2, 0);
    cc_register_cfunc(vm, "seekdir",    (void*)seekdir,     2, 0);
    cc_register_cfunc(vm, "telldir",    (void*)telldir,     1, 0);
    cc_register_cfunc(vm, "rewinddir",  (void*)rewinddir,   1, 0);
    cc_register_cfunc(vm, "getpwuid", (void*)getpwuid, 1, 0);
    cc_register_cfunc(vm, "getpwnam", (void*)getpwnam, 1, 0);
    cc_register_cfunc(vm, "getgrgid", (void*)getgrgid, 1, 0);
    cc_register_cfunc(vm, "getgrnam", (void*)getgrnam, 1, 0);
    cc_register_cfunc(vm, "getpwuid_r", (void*)getpwuid_r, 5, 0);
    cc_register_cfunc(vm, "getpwnam_r", (void*)getpwnam_r, 5, 0);
    cc_register_cfunc(vm, "getgrgid_r", (void*)getgrgid_r, 5, 0);
    cc_register_cfunc(vm, "getgrnam_r", (void*)getgrnam_r, 5, 0);
    cc_register_cfunc(vm, "regcomp",  (void*)regcomp,  3, 0);
    cc_register_cfunc(vm, "regexec",  (void*)regexec,  5, 0);
    cc_register_cfunc(vm, "regerror", (void*)regerror, 4, 0);
    cc_register_cfunc(vm, "regfree",  (void*)wrap_regfree,  1, 0);
    cc_register_cfunc(vm, "glob",     (void*)wrap_glob, 4, 0);
    cc_register_cfunc(vm, "globfree", (void*)wrap_globfree, 1, 0);
    cc_register_cfunc(vm, "scandir",  (void*)wrap_scandir, 4, 0);
}
#else
void register_posix_functions(VirtualMachine *vm) {
    (void)vm;
}
#endif
