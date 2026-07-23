// POSIX and dlfcn stdlib function registration
#include "../cccc.h"
#include "../internal.h"

#if !defined(_WIN32) && !defined(_WIN64)
#include <arpa/inet.h>
#include <dirent.h>
#include <dlfcn.h>
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

void register_posix_functions(VirtualMachine *vm) {
    // Blocking I/O — GIL released while blocked so other VM threads can run
    cc_register_cfunc(vm, "read",    (void*)wrap_read_gil,    3, 0);
    cc_register_cfunc(vm, "write",   (void*)wrap_write_gil,   3, 0);
    cc_register_cfunc(vm, "pread",   (void*)wrap_pread_gil,   4, 0);
    cc_register_cfunc(vm, "pwrite",  (void*)wrap_pwrite_gil,  4, 0);
    cc_register_cfunc(vm, "poll",    (void*)wrap_poll_gil,    3, 0);
    cc_register_cfunc(vm, "accept",  (void*)wrap_accept_gil,  3, 0);
    cc_register_cfunc(vm, "connect", (void*)wrap_connect_gil, 3, 0);
    cc_register_cfunc(vm, "wait",    (void*)wrap_wait_gil,    1, 0);
    cc_register_cfunc(vm, "waitpid", (void*)wrap_waitpid_gil, 3, 0);
    cc_register_cfunc(vm, "sleep",   (void*)wrap_sleep_gil,   1, 0);
    cc_register_cfunc(vm, "usleep",  (void*)wrap_usleep_gil,  1, 0);
    cc_register_cfunc(vm, "nanosleep",(void*)wrap_nanosleep_gil, 2, 0);

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
    cc_register_cfunc(vm, "sysconf",  (void*)sysconf,  1, 0);
    cc_register_cfunc(vm, "mkstemp",  (void*)mkstemp,  1, 0);
    cc_register_cfunc(vm, "mkdtemp",  (void*)mkdtemp,  1, 0);
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
    cc_register_cfunc(vm, "gettimeofday", (void*)gettimeofday, 2, 0);
    cc_register_cfunc(vm, "settimeofday", (void*)settimeofday, 2, 0);
    cc_register_cfunc(vm, "mmap",         (void*)mmap,         6, 0);
    cc_register_cfunc(vm, "munmap",       (void*)munmap,       2, 0);
    cc_register_cfunc(vm, "mprotect",     (void*)mprotect,     3, 0);
    cc_register_cfunc(vm, "msync",        (void*)msync,        3, 0);
    cc_register_cfunc(vm, "posix_madvise",(void*)posix_madvise,3, 0);
#ifdef __linux__
    // mremap: Linux-only glibc/syscall extension for resizing an existing
    // mapping. Forward-declared here (rather than defining _GNU_SOURCE
    // globally) because the host <sys/mman.h> only exposes this prototype
    // under _GNU_SOURCE; glibc still exports the symbol regardless. Needed
    // so cccc-compiled Linux code (e.g. SQLite's unix VFS syscall table)
    // that calls mremap can link (#729).
    extern void *mremap(void *old_address, size_t old_size, size_t new_size, int flags, ...);
    cc_register_cfunc(vm, "mremap", (void*)mremap, 4, 0);
#endif
    cc_register_cfunc(vm, "stat",    (void*)stat,    2, 0);
    cc_register_cfunc(vm, "fstat",   (void*)fstat,   2, 0);
    cc_register_cfunc(vm, "lstat",   (void*)lstat,   2, 0);
    cc_register_cfunc(vm, "chmod",   (void*)chmod,   2, 0);
    cc_register_cfunc(vm, "fchmod",  (void*)fchmod,  2, 0);
    cc_register_cfunc(vm, "fchown",  (void*)fchown,  3, 0);
    cc_register_cfunc(vm, "geteuid", (void*)geteuid, 0, 0);
    cc_register_cfunc(vm, "readlink",(void*)readlink,3, 0);
    cc_register_cfunc(vm, "getpagesize",(void*)getpagesize,0, 0);
    cc_register_cfunc(vm, "mkdir",   (void*)mkdir,   2, 0);
    cc_register_cfunc(vm, "mkfifo",  (void*)mkfifo,  2, 0);
    cc_register_cfunc(vm, "umask",   (void*)wrap_umask,  1, 0);
    cc_register_cfunc(vm, "utime",   (void*)utime,       2, 0);
    cc_register_cfunc(vm, "utimes",  (void*)utimes,      2, 0);
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
    cc_register_cfunc(vm, "bind",        (void*)bind,        3, 0);
    cc_register_cfunc(vm, "listen",      (void*)listen,      2, 0);
    cc_register_cfunc(vm, "setsockopt",  (void*)setsockopt,  5, 0);
    cc_register_cfunc(vm, "getsockname", (void*)getsockname, 3, 0);
    cc_register_cfunc(vm, "shutdown",    (void*)shutdown,    2, 0);
    cc_register_cfunc(vm, "gethostbyname",(void*)gethostbyname, 1, 0);
    cc_register_cfunc(vm, "getaddrinfo", (void*)getaddrinfo,    4, 0);
    cc_register_cfunc(vm, "freeaddrinfo",(void*)wrap_freeaddrinfo, 1, 0);

    cc_register_cfunc(vm, "opendir",  (void*)opendir,  1, 0);
    cc_register_cfunc(vm, "readdir",  (void*)readdir,  1, 0);
    cc_register_cfunc(vm, "closedir", (void*)closedir, 1, 0);
    cc_register_cfunc(vm, "tcgetattr",(void*)tcgetattr, 2, 0);
    cc_register_cfunc(vm, "tcsetattr",(void*)tcsetattr, 3, 0);
    cc_register_cfunc(vm, "getpwuid", (void*)getpwuid, 1, 0);
    cc_register_cfunc(vm, "getpwnam", (void*)getpwnam, 1, 0);
    cc_register_cfunc(vm, "getgrgid", (void*)getgrgid, 1, 0);
    cc_register_cfunc(vm, "getgrnam", (void*)getgrnam, 1, 0);
    cc_register_cfunc(vm, "regcomp",  (void*)regcomp,  3, 0);
    cc_register_cfunc(vm, "regexec",  (void*)regexec,  5, 0);
    cc_register_cfunc(vm, "regerror", (void*)regerror, 4, 0);
    cc_register_cfunc(vm, "regfree",  (void*)wrap_regfree,  1, 0);
    cc_register_cfunc(vm, "glob",     (void*)glob,     4, 0);
    cc_register_cfunc(vm, "globfree", (void*)wrap_globfree, 1, 0);
}
#else
void register_posix_functions(VirtualMachine *vm) {
    (void)vm;
}
#endif
