// posix_io.c -- blocking fd I/O, open/creat/ioctl, pass-through fd/file
// wrappers, and host-global (errno/getopt) accessor pointers (#946 split
// of posix.c).
#include "posix_util.h"

#if !defined(_WIN32) && !defined(_WIN64)

// ---------------------------------------------------------------------------
// Blocking I/O wrappers — release the GIL while the call may block
// ---------------------------------------------------------------------------

static long long wrap_read_gil(long long fd, long long buf, long long count) {
    VirtualMachine *vm = cccc_posix_current_vm();
    if (!vm || !vm->gil_initialized)
        return (long long)read((int)fd, (void *)buf, (size_t)count);
    ExecState state;
    cccc_posix_save_and_release_gil(vm, &state);
    ssize_t r = read((int)fd, (void *)buf, (size_t)count);
    cccc_posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

static long long wrap_write_gil(long long fd, long long buf, long long count) {
    VirtualMachine *vm = cccc_posix_current_vm();
    if (!vm || !vm->gil_initialized)
        return (long long)write((int)fd, (const void *)buf, (size_t)count);
    ExecState state;
    cccc_posix_save_and_release_gil(vm, &state);
    ssize_t r = write((int)fd, (const void *)buf, (size_t)count);
    cccc_posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

static long long wrap_pwrite_gil(long long fd, long long buf, long long count,
                                 long long offset) {
    VirtualMachine *vm = cccc_posix_current_vm();
    if (!vm || !vm->gil_initialized)
        return (long long)pwrite((int)fd, (const void *)buf, (size_t)count,
                                 (off_t)offset);
    ExecState state;
    cccc_posix_save_and_release_gil(vm, &state);
    ssize_t r =
        pwrite((int)fd, (const void *)buf, (size_t)count, (off_t)offset);
    cccc_posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

static long long wrap_nanosleep_gil(long long req, long long rem) {
    VirtualMachine *vm = cccc_posix_current_vm();
    if (!vm || !vm->gil_initialized)
        return (long long)nanosleep((const struct timespec *)req,
                                    (struct timespec *)rem);
    ExecState state;
    cccc_posix_save_and_release_gil(vm, &state);
    int r = nanosleep((const struct timespec *)req, (struct timespec *)rem);
    cccc_posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

static long long wrap_pread_gil(long long fd, long long buf, long long count,
                                long long offset) {
    VirtualMachine *vm = cccc_posix_current_vm();
    if (!vm || !vm->gil_initialized)
        return (long long)pread((int)fd, (void *)buf, (size_t)count,
                                (off_t)offset);
    ExecState state;
    cccc_posix_save_and_release_gil(vm, &state);
    ssize_t r = pread((int)fd, (void *)buf, (size_t)count, (off_t)offset);
    cccc_posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

// readv/writev -- scatter/gather I/O at the fd's own file position.
// Blocking, like read/write above, so they release the GIL the same way.
// struct iovec needs no guest/host translation (void* + size_t, identical
// on both hosts), so unlike ioctl below there is no layout risk here.
static long long wrap_readv_gil(long long fd, long long iov, long long iovcnt) {
    VirtualMachine *vm = cccc_posix_current_vm();
    if (!vm || !vm->gil_initialized)
        return (long long)readv((int)fd, (const struct iovec *)iov,
                                (int)iovcnt);
    ExecState state;
    cccc_posix_save_and_release_gil(vm, &state);
    ssize_t r = readv((int)fd, (const struct iovec *)iov, (int)iovcnt);
    cccc_posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

static long long wrap_writev_gil(long long fd, long long iov,
                                 long long iovcnt) {
    VirtualMachine *vm = cccc_posix_current_vm();
    if (!vm || !vm->gil_initialized)
        return (long long)writev((int)fd, (const struct iovec *)iov,
                                 (int)iovcnt);
    ExecState state;
    cccc_posix_save_and_release_gil(vm, &state);
    ssize_t r = writev((int)fd, (const struct iovec *)iov, (int)iovcnt);
    cccc_posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

// preadv/pwritev (#793) -- the readv/writev analogs of pread/pwrite:
// scatter/gather I/O at an explicit offset. Blocking, like pread/pwrite
// above, so they release the GIL the same way. struct iovec needs no
// guest/host translation (void* + size_t, identical on both hosts), so
// unlike ioctl below there is no layout risk here.
static long long wrap_preadv_gil(long long fd, long long iov, long long iovcnt,
                                 long long offset) {
    VirtualMachine *vm = cccc_posix_current_vm();
    if (!vm || !vm->gil_initialized)
        return (long long)preadv((int)fd, (const struct iovec *)iov,
                                 (int)iovcnt, (off_t)offset);
    ExecState state;
    cccc_posix_save_and_release_gil(vm, &state);
    ssize_t r =
        preadv((int)fd, (const struct iovec *)iov, (int)iovcnt, (off_t)offset);
    cccc_posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

static long long wrap_pwritev_gil(long long fd, long long iov, long long iovcnt,
                                  long long offset) {
    VirtualMachine *vm = cccc_posix_current_vm();
    if (!vm || !vm->gil_initialized)
        return (long long)pwritev((int)fd, (const struct iovec *)iov,
                                  (int)iovcnt, (off_t)offset);
    ExecState state;
    cccc_posix_save_and_release_gil(vm, &state);
    ssize_t r =
        pwritev((int)fd, (const struct iovec *)iov, (int)iovcnt, (off_t)offset);
    cccc_posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

#ifdef __linux__
// preadv2/pwritev2 (#793) -- same as preadv/pwritev plus an RWF_* flags
// word; Linux-only syscalls (glibc >= 2.26), same gap class as
// mremap/fallocate/splice registered further down. Forward-declared
// locally for the same reason (host <sys/uio.h> gates these behind
// _GNU_SOURCE; glibc exports them regardless).
extern ssize_t preadv2(int fd, const struct iovec *iov, int iovcnt,
                       off_t offset, int flags);
extern ssize_t pwritev2(int fd, const struct iovec *iov, int iovcnt,
                        off_t offset, int flags);

static long long wrap_preadv2_gil(long long fd, long long iov, long long iovcnt,
                                  long long offset, long long flags) {
    VirtualMachine *vm = cccc_posix_current_vm();
    if (!vm || !vm->gil_initialized)
        return (long long)preadv2((int)fd, (const struct iovec *)iov,
                                  (int)iovcnt, (off_t)offset, (int)flags);
    ExecState state;
    cccc_posix_save_and_release_gil(vm, &state);
    ssize_t r = preadv2((int)fd, (const struct iovec *)iov, (int)iovcnt,
                        (off_t)offset, (int)flags);
    cccc_posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

static long long wrap_pwritev2_gil(long long fd, long long iov,
                                   long long iovcnt, long long offset,
                                   long long flags) {
    VirtualMachine *vm = cccc_posix_current_vm();
    if (!vm || !vm->gil_initialized)
        return (long long)pwritev2((int)fd, (const struct iovec *)iov,
                                   (int)iovcnt, (off_t)offset, (int)flags);
    ExecState state;
    cccc_posix_save_and_release_gil(vm, &state);
    ssize_t r = pwritev2((int)fd, (const struct iovec *)iov, (int)iovcnt,
                         (off_t)offset, (int)flags);
    cccc_posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}
#endif

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

// wrap_ioctl (#795): request-code allowlist. See the registration comment
// (register_posix_functions) for why this exists; see include/sys/ioctl.h
// for the allowlisted constants themselves. Switches on `request` before
// touching va_arg at all, since the no-arg requests (TIOCSCTTY/TIOCNOTTY)
// must not consume a variadic argument the guest never passed.
static long long wrap_ioctl(long long fd, long long request, ...) {
    va_list ap;
    switch ((unsigned long)request) {
        case TIOCGWINSZ:
        case TIOCSWINSZ:
        case FIONREAD:
        case FIONBIO: {
            va_start(ap, request);
            void *arg = va_arg(ap, void *);
            va_end(ap);
            return (long long)ioctl((int)fd, (unsigned long)request, arg);
        }
        case TIOCSCTTY:
        case TIOCNOTTY:
            return (long long)ioctl((int)fd, (unsigned long)request);
        default:
            break;
    }

    VirtualMachine *vm = cccc_posix_current_vm();
    if (vm && (vm->flags & CCCC_POSIX_EMULATION)) {
        // Opted into raw passthrough (--posix-emulation) -- same
        // unverified-layout risk as before #795, now explicit.
        va_start(ap, request);
        void *arg = va_arg(ap, void *);
        va_end(ap);
        return (long long)ioctl((int)fd, (unsigned long)request, arg);
    }

    // Rejected: this request code's guest/host argument layout has not
    // been verified. One-shot diagnostic (not per-call) so a guest loop
    // calling an unsupported ioctl in a hot path can't spam stderr.
    static int warned = 0;
    if (!warned) {
        warned = 1;
        fprintf(stderr,
                "cccc: ioctl() request 0x%lx is not in the verified allowlist "
                "(see include/sys/ioctl.h); rejecting to avoid a guest/host "
                "struct-layout mismatch. Pass --posix-emulation to allow raw "
                "passthrough at your own risk.\n",
                (unsigned long)request);
    }
    errno = EINVAL;
    return -1;
}

// The following wrappers are non-blocking or return immediately; they
// intentionally hold the GIL: close, lseek, access, unlink, rmdir, chdir,
// getcwd, stat/fstat/lstat, chmod, mkdir, mkfifo, umask, pipe, fork, _exit,
// execv/execve/execl/execlp/execle/execvp (exec replaces the process),
// mmap/munmap/mprotect/msync/posix_madvise (kernel operations, not blocking),
// socket/bind/listen/shutdown/setsockopt/getsockname (non-blocking control
// ops), opendir/readdir/closedir, tcgetattr/tcsetattr,
// getpwuid/getpwnam/getgrgid/getgrnam,
// regcomp/regexec/regerror/regfree/glob/globfree (CPU-bound, no I/O wait),
// string/network byte-order helpers.

static long long wrap_close(long long fd) {
    return (long long)close((int)fd);
}
static long long wrap_lseek(long long fd, long long offset, long long whence) {
    return (long long)lseek((int)fd, (off_t)offset, (int)whence);
}
static long long wrap_access(long long path, long long mode) {
    return (long long)access((const char *)path, (int)mode);
}
static long long wrap_unlink(long long path) {
    return (long long)unlink((const char *)path);
}
static long long wrap_rmdir(long long path) {
    return (long long)rmdir((const char *)path);
}
static long long wrap_chdir(long long path) {
    return (long long)chdir((const char *)path);
}
static long long wrap_fork(void) {
    return (long long)fork();
}
static long long wrap_pipe(long long fd) {
    return (long long)pipe((int *)fd);
}
static long long wrap__exit(long long status) {
    _exit((int)status);
    return 0;
}

static long long wrap_umask(long long cmask) {
    return (long long)umask((mode_t)cmask);
}
static long long wrap_basename(long long path) {
    return (long long)basename((char *)path);
}
static long long wrap_dirname(long long path) {
    return (long long)dirname((char *)path);
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
// Named wrap_* rather than __cccc_*_ptr: under self-hosting, include/errno.h
// / include/getopt.h expand errno/optarg/optind/opterr/optopt to
// `(*__cccc_..._ptr())`, so a host-side definition spelled the same way
// would (a) collide at parse time with its own extern declaration -- a
// static definition following a non-static extern is a constraint violation
// -- and (b) recurse into itself once the linkage was reconciled, since the
// macro expansion inside the accessor's own body would call itself (#1280).
// The FFI registration name below is a string, independent of the C
// identifier, so the guest-visible name is unaffected.
static int *wrap_errno_ptr(void) {
    return &errno;
}
static char **wrap_optarg_ptr(void) {
    return &optarg;
}
static int *wrap_optind_ptr(void) {
    return &optind;
}
static int *wrap_opterr_ptr(void) {
    return &opterr;
}
static int *wrap_optopt_ptr(void) {
    return &optopt;
}
// #957: environ (declared via the same accessor-macro pattern in
// include/unistd.h) -- extern char **environ is the host's real process
// environment array here, declared by the host <unistd.h> included above
// (via posix_util.h).
extern char **environ;
static char ***wrap_environ_ptr(void) {
    return &environ;
}

void register_posix_io_functions(VirtualMachine *vm) {
    // Blocking I/O — GIL released while blocked so other VM threads can run
    cc_register_cfunc(vm, "read", (void *)wrap_read_gil, 3, 0);
    cc_register_cfunc(vm, "write", (void *)wrap_write_gil, 3, 0);
    cc_register_cfunc(vm, "pread", (void *)wrap_pread_gil, 4, 0);
    cc_register_cfunc(vm, "pwrite", (void *)wrap_pwrite_gil, 4, 0);
    cc_register_cfunc(vm, "nanosleep", (void *)wrap_nanosleep_gil, 2, 0);
    // readv/writev/preadv/pwritev release the GIL like read/write/pread/pwrite.
    cc_register_cfunc(vm, "readv", (void *)wrap_readv_gil, 3, 0);
    cc_register_cfunc(vm, "writev", (void *)wrap_writev_gil, 3, 0);
    cc_register_cfunc(vm, "preadv", (void *)wrap_preadv_gil, 4, 0);
    cc_register_cfunc(vm, "pwritev", (void *)wrap_pwritev_gil, 4, 0);

    // Non-blocking / fast — intentionally keep the GIL
    cc_register_cfunc(vm, "close", (void *)wrap_close, 1, 0);
    cc_register_cfunc(vm, "lseek", (void *)wrap_lseek, 3, 0);
    cc_register_cfunc(vm, "access", (void *)wrap_access, 2, 0);
    cc_register_cfunc(vm, "unlink", (void *)wrap_unlink, 1, 0);
    cc_register_cfunc(vm, "rmdir", (void *)wrap_rmdir, 1, 0);
    cc_register_cfunc(vm, "chdir", (void *)wrap_chdir, 1, 0);
    cc_register_cfunc(vm, "getcwd", (void *)getcwd, 2, 0);
    cc_register_cfunc(vm, "getpid", (void *)getpid, 0, 0);
    cc_register_cfunc(vm, "getppid", (void *)getppid, 0, 0);
    cc_register_cfunc(vm, "fork", (void *)wrap_fork, 0, 0);
    cc_register_cfunc(vm, "pipe", (void *)wrap_pipe, 1, 0);
    cc_register_cfunc(vm, "_exit", (void *)wrap__exit, 1, 0);
    cc_register_cfunc(vm, "execv", (void *)execv, 2, 0);
    cc_register_cfunc(vm, "execve", (void *)execve, 3, 0);
    cc_register_variadic_cfunc(vm, "execl", (void *)execl, 2, 0);
    cc_register_variadic_cfunc(vm, "execlp", (void *)execlp, 2, 0);
    cc_register_variadic_cfunc(vm, "execle", (void *)execle, 2, 0);
    cc_register_cfunc(vm, "execvp", (void *)execvp, 2, 0);
    cc_register_cfunc(vm, "isatty", (void *)isatty, 1, 0);
    cc_register_cfunc(vm, "ttyname", (void *)ttyname, 1, 0);
    cc_register_cfunc(vm, "dup", (void *)dup, 1, 0);
    cc_register_cfunc(vm, "dup2", (void *)dup2, 2, 0);
    cc_register_cfunc(vm, "fsync", (void *)fsync, 1, 0);
#ifdef __linux__
    // fdatasync: POSIX, but absent from Darwin's libc entirely (macOS has
    // no equivalent syscall/libc symbol at all, unlike fsync -- the
    // closest analog is the fcntl F_FULLFSYNC command, not a drop-in
    // replacement). Guest-side declaration in include/unistd.h is
    // __linux__-guarded to match (#783).
    cc_register_cfunc(vm, "fdatasync", (void *)fdatasync, 1, 0);
#endif
    cc_register_cfunc(vm, "ftruncate", (void *)ftruncate, 2, 0);
    cc_register_cfunc(vm, "truncate", (void *)truncate, 2, 0);
    cc_register_cfunc(vm, "mkstemp", (void *)mkstemp, 1, 0);
    cc_register_cfunc(vm, "mkdtemp", (void *)mkdtemp, 1, 0);
    cc_register_cfunc(vm, "seteuid", (void *)seteuid, 1, 0);
    cc_register_cfunc(vm, "setegid", (void *)setegid, 1, 0);
    cc_register_cfunc(vm, "setuid", (void *)setuid, 1, 0);
    cc_register_cfunc(vm, "setgid", (void *)setgid, 1, 0);
    cc_register_cfunc(vm, "getgroups", (void *)getgroups, 2, 0);
    cc_register_cfunc(vm, "getlogin", (void *)getlogin, 0, 0);
    cc_register_cfunc(vm, "link", (void *)link, 2, 0);
    cc_register_cfunc(vm, "getpgid", (void *)getpgid, 1, 0);
    cc_register_cfunc(vm, "setpgid", (void *)setpgid, 2, 0);
    cc_register_cfunc(vm, "getpgrp", (void *)getpgrp, 0, 0);
    cc_register_cfunc(vm, "setsid", (void *)setsid, 0, 0);
    cc_register_cfunc(vm, "getsid", (void *)getsid, 1, 0);
    cc_register_cfunc(vm, "alarm", (void *)alarm, 1, 0);
    cc_register_cfunc(vm, "fchdir", (void *)fchdir, 1, 0);
    cc_register_cfunc(vm, "gethostname", (void *)gethostname, 2, 0);
    cc_register_cfunc(vm, "sethostname", (void *)sethostname, 2, 0);
    cc_register_cfunc(vm, "lchown", (void *)lchown, 3, 0);
    cc_register_cfunc(vm, "chown", (void *)chown, 3,
                      0); // #783: declared, never registered
    cc_register_cfunc(vm, "ttyname_r", (void *)ttyname_r, 3, 0);
    cc_register_cfunc(vm, "getlogin_r", (void *)getlogin_r, 2, 0);
    cc_register_cfunc(vm, "setgroups", (void *)setgroups, 2, 0);
    cc_register_cfunc(vm, "initgroups", (void *)initgroups, 2, 0);
    cc_register_cfunc(vm, "nice", (void *)nice, 1, 0);
    cc_register_variadic_cfunc(vm, "open", (void *)wrap_open, 2, 0);
    cc_register_cfunc(vm, "creat", (void *)wrap_creat, 2, 0);
    cc_register_variadic_cfunc(vm, "fcntl", (void *)fcntl, 2, 0);
    // flock/ioctl: declared in include/ but never registered anywhere --
    // same "undefined function" gap class as #783, surfaced by widening
    // tools/audit_ffi.py's header scan for #784/#792.
    cc_register_cfunc(vm, "flock", (void *)flock, 2, 0);
    // ioctl (#795): request-code allowlist, not a raw passthrough -- see
    // wrap_ioctl's own comment above for why.
    cc_register_variadic_cfunc(vm, "ioctl", (void *)wrap_ioctl, 2, 0);
    cc_register_cfunc(vm, "umask", (void *)wrap_umask, 1, 0);

    // mmap et al (sys/mman.h) -- kernel operations, not blocking.
    cc_register_cfunc(vm, "gettimeofday", (void *)gettimeofday, 2, 0);
    cc_register_cfunc(vm, "settimeofday", (void *)settimeofday, 2, 0);
    cc_register_cfunc(vm, "mmap", (void *)mmap, 6, 0);
    cc_register_cfunc(vm, "munmap", (void *)munmap, 2, 0);
    cc_register_cfunc(vm, "mprotect", (void *)mprotect, 3, 0);
    cc_register_cfunc(vm, "msync", (void *)msync, 3, 0);
    cc_register_cfunc(vm, "posix_madvise", (void *)posix_madvise, 3, 0);
    cc_register_cfunc(vm, "mlock", (void *)mlock, 2, 0);
    cc_register_cfunc(vm, "munlock", (void *)munlock, 2, 0);
    cc_register_cfunc(vm, "mlockall", (void *)mlockall, 1, 0);
    cc_register_cfunc(vm, "munlockall", (void *)munlockall, 0, 0);
    cc_register_cfunc(vm, "shm_open", (void *)shm_open, 3, 0);
    cc_register_cfunc(vm, "shm_unlink", (void *)shm_unlink, 1, 0);
#ifdef __linux__
    // mremap: Linux-only glibc/syscall extension for resizing an existing
    // mapping. Forward-declared here (rather than defining _GNU_SOURCE
    // globally) because the host <sys/mman.h> only exposes this prototype
    // under _GNU_SOURCE; glibc still exports the symbol regardless. Needed
    // so cccc-compiled Linux code (e.g. SQLite's unix VFS syscall table)
    // that calls mremap can link (#729).
    extern void *mremap(void *old_address, size_t old_size, size_t new_size,
                        int flags, ...);
    cc_register_cfunc(vm, "mremap", (void *)mremap, 4, 0);
    // fallocate/splice: same gap class as mremap above -- Linux-only libc
    // calls SQLite's unix VFS can reference (behind HAVE_FALLOCATE config,
    // not currently active in the smoke build) that were previously
    // undeclared and unregistered anywhere in include/ or src/stdlib/ (#731).
    extern int fallocate(int fd, int mode, off_t offset, off_t len);
    cc_register_cfunc(vm, "fallocate", (void *)fallocate, 4, 0);
    extern ssize_t splice(int fd_in, off_t *off_in, int fd_out, off_t *off_out,
                          size_t len, unsigned int flags);
    cc_register_cfunc(vm, "splice", (void *)splice, 6, 0);
    // preadv2/pwritev2 (#793): declared+wrapped above, next to preadv/pwritev.
    cc_register_cfunc(vm, "preadv2", (void *)wrap_preadv2_gil, 5, 0);
    cc_register_cfunc(vm, "pwritev2", (void *)wrap_pwritev2_gil, 5, 0);
#endif
    cc_register_cfunc(vm, "stat", (void *)stat, 2, 0);
    cc_register_cfunc(vm, "fstat", (void *)fstat, 2, 0);
    cc_register_cfunc(vm, "lstat", (void *)lstat, 2, 0);
    cc_register_cfunc(vm, "fstatat", (void *)fstatat, 4, 0);
    cc_register_cfunc(vm, "chmod", (void *)chmod, 2, 0);
    cc_register_cfunc(vm, "fchmod", (void *)fchmod, 2, 0);
    cc_register_cfunc(vm, "fchmodat", (void *)fchmodat, 4, 0);
    cc_register_cfunc(vm, "fchown", (void *)fchown, 3, 0);
    cc_register_cfunc(vm, "geteuid", (void *)geteuid, 0, 0);
    cc_register_cfunc(vm, "getuid", (void *)getuid, 0, 0);
    cc_register_cfunc(vm, "getgid", (void *)getgid, 0, 0);
    cc_register_cfunc(vm, "getegid", (void *)getegid, 0, 0);
    cc_register_cfunc(vm, "readlink", (void *)readlink, 3, 0);
    cc_register_cfunc(vm, "symlink", (void *)symlink, 2,
                      0); // #783: declared, never registered
    cc_register_cfunc(vm, "getpagesize", (void *)getpagesize, 0, 0);
    cc_register_cfunc(vm, "mkdir", (void *)mkdir, 2, 0);
    cc_register_cfunc(vm, "mkdirat", (void *)mkdirat, 3, 0);
    cc_register_cfunc(vm, "mkfifo", (void *)mkfifo, 2, 0);
    cc_register_cfunc(vm, "mknod", (void *)mknod, 3, 0);
    cc_register_cfunc(vm, "utime", (void *)utime, 2, 0);
    cc_register_cfunc(vm, "utimes", (void *)utimes, 2, 0);
    cc_register_cfunc(vm, "futimes", (void *)futimes, 2, 0);
    cc_register_cfunc(vm, "lutimes", (void *)lutimes, 2, 0);
    cc_register_cfunc(vm, "setitimer", (void *)setitimer, 3, 0);
    cc_register_cfunc(vm, "getitimer", (void *)getitimer, 2, 0);

    // struct rlimit / RLIMIT_* / getpriority/setpriority (#786) -- fast,
    // local, no GIL release needed.
    cc_register_cfunc(vm, "getrlimit", (void *)getrlimit, 2, 0);
    cc_register_cfunc(vm, "setrlimit", (void *)setrlimit, 2, 0);
    cc_register_cfunc(vm, "getpriority", (void *)getpriority, 2, 0);
    cc_register_cfunc(vm, "setpriority", (void *)setpriority, 3, 0);

    // sys/utsname.h uname() and sys/times.h times() (#733/#737) -- fast,
    // local, no GIL release needed.
    cc_register_cfunc(vm, "uname", (void *)uname, 1, 0);
    cc_register_cfunc(vm, "times", (void *)times, 1, 0);

    cc_register_cfunc(vm, "strcasecmp", (void *)strcasecmp, 2, 0);
    cc_register_cfunc(vm, "strncasecmp", (void *)strncasecmp, 3, 0);
    cc_register_cfunc(vm, "bcmp", (void *)bcmp, 3, 0);
    cc_register_cfunc(vm, "index", (void *)index, 2, 0);
    cc_register_cfunc(vm, "rindex", (void *)rindex, 2, 0);
    cc_register_cfunc(vm, "basename", (void *)wrap_basename, 1, 0);
    cc_register_cfunc(vm, "dirname", (void *)wrap_dirname, 1, 0);
    cc_register_cfunc(vm, "fnmatch", (void *)fnmatch, 3, 0);
    cc_register_cfunc(vm, "getopt", (void *)getopt, 3, 0);
    cc_register_cfunc(vm, "getopt_long", (void *)getopt_long, 5, 0);
    cc_register_cfunc(vm, "__cccc_optarg_ptr", (void *)wrap_optarg_ptr, 0, 0);
    cc_register_cfunc(vm, "__cccc_optind_ptr", (void *)wrap_optind_ptr, 0, 0);
    cc_register_cfunc(vm, "__cccc_opterr_ptr", (void *)wrap_opterr_ptr, 0, 0);
    cc_register_cfunc(vm, "__cccc_optopt_ptr", (void *)wrap_optopt_ptr, 0, 0);
    cc_register_cfunc(vm, "__cccc_errno_ptr", (void *)wrap_errno_ptr, 0, 0);
    cc_register_cfunc(vm, "__cccc_environ_ptr", (void *)wrap_environ_ptr, 0, 0);

    // sys/ipc.h etc via posix_ipc.c, sockets via posix_net.c, etc. are
    // registered by their own domain files -- see register_posix_functions
    // in posix.c.

    // termios.h and pwd.h/grp.h -- fast, local, no GIL release needed.
    cc_register_cfunc(vm, "tcgetattr", (void *)tcgetattr, 2, 0);
    cc_register_cfunc(vm, "tcsetattr", (void *)tcsetattr, 3, 0);
    cc_register_cfunc(vm, "cfgetispeed", (void *)cfgetispeed, 1, 0);
    cc_register_cfunc(vm, "cfgetospeed", (void *)cfgetospeed, 1, 0);
    cc_register_cfunc(vm, "cfsetispeed", (void *)cfsetispeed, 2, 0);
    cc_register_cfunc(vm, "cfsetospeed", (void *)cfsetospeed, 2, 0);
    cc_register_cfunc(vm, "cfsetspeed", (void *)cfsetspeed, 2, 0);
    cc_register_cfunc(vm, "cfmakeraw", (void *)cfmakeraw, 1, 0);
    cc_register_cfunc(vm, "tcdrain", (void *)tcdrain, 1, 0);
    cc_register_cfunc(vm, "tcflow", (void *)tcflow, 2, 0);
    cc_register_cfunc(vm, "tcflush", (void *)tcflush, 2, 0);
    cc_register_cfunc(vm, "tcsendbreak", (void *)tcsendbreak, 2, 0);
    cc_register_cfunc(vm, "getpwuid", (void *)getpwuid, 1, 0);
    cc_register_cfunc(vm, "getpwnam", (void *)getpwnam, 1, 0);
    cc_register_cfunc(vm, "getgrgid", (void *)getgrgid, 1, 0);
    cc_register_cfunc(vm, "getgrnam", (void *)getgrnam, 1, 0);
    cc_register_cfunc(vm, "getpwuid_r", (void *)getpwuid_r, 5, 0);
    cc_register_cfunc(vm, "getpwnam_r", (void *)getpwnam_r, 5, 0);
    cc_register_cfunc(vm, "getgrgid_r", (void *)getgrgid_r, 5, 0);
    cc_register_cfunc(vm, "getgrnam_r", (void *)getgrnam_r, 5, 0);
}

#else
void register_posix_io_functions(VirtualMachine *vm) {
    (void)vm;
}
#endif
