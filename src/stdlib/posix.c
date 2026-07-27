// POSIX and dlfcn stdlib function registration
#include "../cccc.h"
#include "../internal.h"

#if !defined(_WIN32) && !defined(_WIN64)
// Must be defined before <netinet/in.h> pulls in the real system header, or
// the advanced IPV6_* options (#749) -- IPV6_PKTINFO, IPV6_TCLASS, etc. --
// won't be visible on macOS (RFC 3542 options are opt-in there; Linux glibc
// exposes them unconditionally).
#define __APPLE_USE_RFC_3542
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
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <pwd.h>
#include <regex.h>
#include <signal.h>
#include <string.h>
#include <strings.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/times.h>
#include <sys/uio.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <syslog.h>
#include <termios.h>
#include <unistd.h>
#include <utime.h>
#ifdef __linux__
#include <sys/vfs.h>
#else
#include <sys/mount.h>
#include <sys/param.h>
#endif

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

// sys/select.h (#798) -- fd_set is byte-identical between the guest's flat
// 128-byte representation and the host's real fd_set on both platforms (the
// underlying word width used to test bits differs -- int32 on macOS, long
// on Linux -- but both are little-endian, so the byte-level bit layout is
// the same either way), so it passes straight through. struct timeval now
// matches the host layout too (see include/sys/time.h). select()/pselect()
// are blocking, so both release the GIL like poll() above.
static long long wrap_select_gil(long long nfds, long long readfds, long long writefds,
                                 long long exceptfds, long long timeout) {
    VirtualMachine *vm = current_vm();
    if (!vm || !vm->gil_initialized)
        return (long long)select((int)nfds, (fd_set *)readfds, (fd_set *)writefds,
                                 (fd_set *)exceptfds, (struct timeval *)timeout);
    ExecState state;
    posix_save_and_release_gil(vm, &state);
    int r = select((int)nfds, (fd_set *)readfds, (fd_set *)writefds,
                   (fd_set *)exceptfds, (struct timeval *)timeout);
    posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

// pselect()'s sigmask can't be passed through: the guest's sigset_t is its
// own 4-byte bitmask (signals 1..31, bit signo-1 -- see signal.c's
// wrap_sigemptyset/wrap_sigaddset/etc.), reimplemented natively rather than
// aliasing the host's real sigset_t (a 128-byte struct on Linux) precisely
// to avoid an OOB read/write of that pointer (#738). So the guest mask is
// translated into a real host sigset_t via the host's own sigemptyset/
// sigaddset before the call -- CCCC's SIG* constants already match the
// host's numbering (include/signal.h is #ifdef __APPLE__-guarded per
// platform), so signo translates unchanged.
static void guest_sigset_to_host(unsigned int guest_mask, sigset_t *host_set) {
    sigemptyset(host_set);
    for (int signo = 1; signo < CCCC_NSIG; signo++) {
        if (guest_mask & (1u << (unsigned)(signo - 1)))
            sigaddset(host_set, signo);
    }
}

static long long wrap_pselect_gil(long long nfds, long long readfds, long long writefds,
                                  long long exceptfds, long long timeout, long long sigmask) {
    sigset_t host_set;
    sigset_t *host_set_ptr = NULL;
    if (sigmask) {
        guest_sigset_to_host(*(unsigned int *)sigmask, &host_set);
        host_set_ptr = &host_set;
    }
    VirtualMachine *vm = current_vm();
    if (!vm || !vm->gil_initialized)
        return (long long)pselect((int)nfds, (fd_set *)readfds, (fd_set *)writefds,
                                  (fd_set *)exceptfds, (const struct timespec *)timeout,
                                  host_set_ptr);
    ExecState state;
    posix_save_and_release_gil(vm, &state);
    int r = pselect((int)nfds, (fd_set *)readfds, (fd_set *)writefds,
                    (fd_set *)exceptfds, (const struct timespec *)timeout, host_set_ptr);
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

static long long wrap_sendmsg_gil(long long sockfd, long long msg, long long flags) {
    VirtualMachine *vm = current_vm();
    if (!vm || !vm->gil_initialized)
        return (long long)sendmsg((int)sockfd, (const struct msghdr *)msg, (int)flags);
    ExecState state;
    posix_save_and_release_gil(vm, &state);
    ssize_t r = sendmsg((int)sockfd, (const struct msghdr *)msg, (int)flags);
    posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

static long long wrap_recvmsg_gil(long long sockfd, long long msg, long long flags) {
    VirtualMachine *vm = current_vm();
    if (!vm || !vm->gil_initialized)
        return (long long)recvmsg((int)sockfd, (struct msghdr *)msg, (int)flags);
    ExecState state;
    posix_save_and_release_gil(vm, &state);
    ssize_t r = recvmsg((int)sockfd, (struct msghdr *)msg, (int)flags);
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

static long long wrap_waitid_gil(long long idtype, long long id, long long infop, long long options) {
    VirtualMachine *vm = current_vm();
    if (!vm || !vm->gil_initialized)
        return (long long)waitid((idtype_t)idtype, (id_t)id, (siginfo_t *)infop, (int)options);
    ExecState state;
    posix_save_and_release_gil(vm, &state);
    int r = waitid((idtype_t)idtype, (id_t)id, (siginfo_t *)infop, (int)options);
    posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

static long long wrap_wait3_gil(long long wstatus, long long options, long long rusage) {
    VirtualMachine *vm = current_vm();
    if (!vm || !vm->gil_initialized)
        return (long long)wait3((int *)wstatus, (int)options, (struct rusage *)rusage);
    ExecState state;
    posix_save_and_release_gil(vm, &state);
    pid_t r = wait3((int *)wstatus, (int)options, (struct rusage *)rusage);
    posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

static long long wrap_wait4_gil(long long pid, long long wstatus, long long options, long long rusage) {
    VirtualMachine *vm = current_vm();
    if (!vm || !vm->gil_initialized)
        return (long long)wait4((pid_t)pid, (int *)wstatus, (int)options, (struct rusage *)rusage);
    ExecState state;
    posix_save_and_release_gil(vm, &state);
    pid_t r = wait4((pid_t)pid, (int *)wstatus, (int)options, (struct rusage *)rusage);
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
// DNS/NSS lookup wrappers (#748) — release the GIL while the call may block
//
// gethostbyname/gethostbyaddr/getaddrinfo/getnameinfo/getnetbyname/
// getnetbyaddr were previously direct (non-GIL-releasing) FFI calls, even
// though real DNS/NSS lookups can block for seconds. Under the VM's
// single-GIL threading model this stalled every other VM thread for the
// duration. Now they follow the same posix_save_and_release_gil /
// posix_acquire_and_restore_gil pattern as recv/send/waitpid/waitid.
//
// Caveat: gethostbyname/gethostbyaddr/getnetbyname/getnetbyaddr return
// pointers into static, non-reentrant host storage. Releasing the GIL means
// two VM threads can now race on that shared buffer where the GIL previously
// serialized them -- inherent to this POSIX API (the _r variants exist for
// exactly this reason); the guest-visible symptom would be one thread
// reading a result that another thread's later call has already overwritten,
// not a hang or corruption of unrelated memory. getaddrinfo/getnameinfo are
// reentrant and unaffected.
//
// nss_static_mutex (#785) additionally serializes these against the
// gethostbyname_r/gethostbyaddr_r/getnetbyname_r shims below, so the static
// buffer is at least never *written* concurrently -- the plain functions'
// returned pointer is still only valid until the next call from any thread
// on any of these six functions, which the mutex cannot fix; that's what
// the _r variants are for.
#if !defined(_WIN32) && !defined(_WIN64)
static pthread_mutex_t nss_static_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

static long long wrap_gethostbyname_gil(long long name) {
    VirtualMachine *vm = current_vm();
    if (!vm || !vm->gil_initialized)
        return (long long)gethostbyname((const char *)name);
    ExecState state;
    posix_save_and_release_gil(vm, &state);
#if !defined(_WIN32) && !defined(_WIN64)
    pthread_mutex_lock(&nss_static_mutex);
#endif
    struct hostent *r = gethostbyname((const char *)name);
#if !defined(_WIN32) && !defined(_WIN64)
    pthread_mutex_unlock(&nss_static_mutex);
#endif
    posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

static long long wrap_gethostbyaddr_gil(long long addr, long long len, long long type) {
    VirtualMachine *vm = current_vm();
    if (!vm || !vm->gil_initialized)
        return (long long)gethostbyaddr((const void *)addr, (socklen_t)len, (int)type);
    ExecState state;
    posix_save_and_release_gil(vm, &state);
#if !defined(_WIN32) && !defined(_WIN64)
    pthread_mutex_lock(&nss_static_mutex);
#endif
    struct hostent *r = gethostbyaddr((const void *)addr, (socklen_t)len, (int)type);
#if !defined(_WIN32) && !defined(_WIN64)
    pthread_mutex_unlock(&nss_static_mutex);
#endif
    posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

static long long wrap_getaddrinfo_gil(long long node, long long service, long long hints, long long res) {
    VirtualMachine *vm = current_vm();
    if (!vm || !vm->gil_initialized)
        return (long long)getaddrinfo((const char *)node, (const char *)service,
                                       (const struct addrinfo *)hints, (struct addrinfo **)res);
    ExecState state;
    posix_save_and_release_gil(vm, &state);
    int r = getaddrinfo((const char *)node, (const char *)service,
                         (const struct addrinfo *)hints, (struct addrinfo **)res);
    posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

static long long wrap_getnameinfo_gil(long long addr, long long addrlen, long long host, long long hostlen,
                                       long long serv, long long servlen, long long flags) {
    VirtualMachine *vm = current_vm();
    if (!vm || !vm->gil_initialized)
        return (long long)getnameinfo((const struct sockaddr *)addr, (socklen_t)addrlen,
                                       (char *)host, (socklen_t)hostlen,
                                       (char *)serv, (socklen_t)servlen, (int)flags);
    ExecState state;
    posix_save_and_release_gil(vm, &state);
    int r = getnameinfo((const struct sockaddr *)addr, (socklen_t)addrlen,
                         (char *)host, (socklen_t)hostlen,
                         (char *)serv, (socklen_t)servlen, (int)flags);
    posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

static long long wrap_getnetbyname_gil(long long name) {
    VirtualMachine *vm = current_vm();
    if (!vm || !vm->gil_initialized)
        return (long long)getnetbyname((const char *)name);
    ExecState state;
    posix_save_and_release_gil(vm, &state);
#if !defined(_WIN32) && !defined(_WIN64)
    pthread_mutex_lock(&nss_static_mutex);
#endif
    struct netent *r = getnetbyname((const char *)name);
#if !defined(_WIN32) && !defined(_WIN64)
    pthread_mutex_unlock(&nss_static_mutex);
#endif
    posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

static long long wrap_getnetbyaddr_gil(long long net, long long type) {
    VirtualMachine *vm = current_vm();
    if (!vm || !vm->gil_initialized)
        return (long long)getnetbyaddr((uint32_t)net, (int)type);
    ExecState state;
    posix_save_and_release_gil(vm, &state);
#if !defined(_WIN32) && !defined(_WIN64)
    pthread_mutex_lock(&nss_static_mutex);
#endif
    struct netent *r = getnetbyaddr((uint32_t)net, (int)type);
#if !defined(_WIN32) && !defined(_WIN64)
    pthread_mutex_unlock(&nss_static_mutex);
#endif
    posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

// ---------------------------------------------------------------------------
// gethostbyname_r/gethostbyaddr_r/getnetbyname_r (#785) — race-free
// alternative to the static-buffer lookups above.
//
// macOS has no gethostbyname_r/gethostbyaddr_r/getnetbyname_r at all (glibc-
// only extensions), so this is one portable shim used on both platforms
// rather than a passthrough to the host's own _r functions: nss_static_mutex
// serializes access to the underlying plain lookup's static buffer (taken
// here AND by the plain wrappers above, so the two families are mutually
// exclusive), and the result is deep-copied into the caller's own buf before
// the mutex is released. This closes the write race the #748 GIL release
// introduced -- concurrent guest threads can no longer see a torn or
// overwritten struct -- but the plain wrappers' *returned pointer* is still
// only valid until the next call from any thread on any of these six
// functions; that part is unfixable for those and is exactly why the _r
// variants exist. See followup ticket for forwarding to glibc's native _r
// functions on Linux instead of this shim, for true reentrancy without
// serialization. nss_static_mutex itself is declared above, next to the
// plain wrappers it also protects.

// Required buffer size for a NULL-terminated char* array of `count` non-NULL
// entries (whose combined string bytes, each including its NUL, are
// `str_bytes`) plus alignment padding for the pointer array itself.
static size_t nss_r_layout_size(int count, size_t str_bytes) {
    size_t ptrs = (size_t)(count + 1) * sizeof(char *);
    return ptrs + sizeof(char *) /* worst-case alignment padding */ + str_bytes;
}

// Copies a NULL-terminated `char **list` (with `count` entries) into `buf`,
// writing the new pointer array at *cursor (advanced past the array +
// alignment) and each string right after. Returns the new array's base, or
// NULL if `end` would be exceeded.
static char **nss_r_copy_ptr_array(char **list, int count, char **cursor, char *end) {
    char *p = (char *)*cursor;
    /* Align the pointer-array base to sizeof(char *). */
    p = (char *)(((uintptr_t)p + sizeof(char *) - 1) & ~(uintptr_t)(sizeof(char *) - 1));
    char **arr = (char **)(void *)p;
    if ((char *)(arr + count + 1) > end) return NULL;
    char *strp = (char *)(arr + count + 1);
    for (int i = 0; i < count; i++) {
        size_t len = strlen(list[i]) + 1;
        if (strp + len > end) return NULL;
        memcpy(strp, list[i], len);
        arr[i] = strp;
        strp += len;
    }
    arr[count] = 0;
    *cursor = strp;
    return arr;
}

static int nss_count_list(char **list) {
    int n = 0;
    if (list) while (list[n]) n++;
    return n;
}

static long long wrap_gethostbyname_r_gil(long long name, long long ret, long long buf,
                                           long long buflen, long long result, long long h_errnop) {
    struct hostent *out = (struct hostent *)ret;
    char *b = (char *)buf;
    size_t blen = (size_t)buflen;
    struct hostent **resultp = (struct hostent **)result;
    int *herrp = (int *)h_errnop;

    VirtualMachine *vm = current_vm();
    ExecState state;
    int gil = vm && vm->gil_initialized;
    if (gil) posix_save_and_release_gil(vm, &state);
#if !defined(_WIN32) && !defined(_WIN64)
    pthread_mutex_lock(&nss_static_mutex);
#endif
    struct hostent *src = gethostbyname((const char *)name);
    int rc = 0;
    if (!src) {
        *resultp = 0;
        if (herrp) *herrp = HOST_NOT_FOUND;
    } else {
        int naliases = nss_count_list(src->h_aliases);
        int naddrs = nss_count_list(src->h_addr_list);
        size_t need = strlen(src->h_name) + 1;
        for (int i = 0; i < naliases; i++) need += strlen(src->h_aliases[i]) + 1;
        need = nss_r_layout_size(naliases, need) +
               nss_r_layout_size(naddrs, (size_t)naddrs * (size_t)src->h_length);
        if (need > blen) {
            rc = ERANGE;
            *resultp = 0;
        } else {
            char *cursor = b;
            char *end = b + blen;
            /* Name string first. */
            size_t namelen = strlen(src->h_name) + 1;
            if (cursor + namelen > end) { rc = ERANGE; *resultp = 0; goto done; }
            memcpy(cursor, src->h_name, namelen);
            out->h_name = cursor;
            cursor += namelen;

            char **aliases = nss_r_copy_ptr_array(src->h_aliases, naliases, &cursor, end);
            if (!aliases) { rc = ERANGE; *resultp = 0; goto done; }
            out->h_aliases = aliases;

            out->h_addrtype = src->h_addrtype;
            out->h_length = src->h_length;

            /* Address list: not NUL-terminated strings but fixed h_length
               byte blobs -- copy manually rather than via
               nss_r_copy_ptr_array's strlen-based packer. */
            char *p = cursor;
            p = (char *)(((uintptr_t)p + sizeof(char *) - 1) & ~(uintptr_t)(sizeof(char *) - 1));
            char **addrs = (char **)(void *)p;
            if ((char *)(addrs + naddrs + 1) > end) { rc = ERANGE; *resultp = 0; goto done; }
            char *ap = (char *)(addrs + naddrs + 1);
            for (int i = 0; i < naddrs; i++) {
                if (ap + src->h_length > end) { rc = ERANGE; *resultp = 0; goto done; }
                memcpy(ap, src->h_addr_list[i], (size_t)src->h_length);
                addrs[i] = ap;
                ap += src->h_length;
            }
            addrs[naddrs] = 0;
            out->h_addr_list = addrs;

            *resultp = out;
        }
    }
done:
#if !defined(_WIN32) && !defined(_WIN64)
    pthread_mutex_unlock(&nss_static_mutex);
#endif
    if (gil) posix_acquire_and_restore_gil(vm, &state);
    return (long long)rc;
}

static long long wrap_gethostbyaddr_r_gil(long long addr, long long len, long long type, long long ret,
                                           long long buf, long long buflen, long long result, long long h_errnop) {
    struct hostent *out = (struct hostent *)ret;
    char *b = (char *)buf;
    size_t blen = (size_t)buflen;
    struct hostent **resultp = (struct hostent **)result;
    int *herrp = (int *)h_errnop;

    VirtualMachine *vm = current_vm();
    ExecState state;
    int gil = vm && vm->gil_initialized;
    if (gil) posix_save_and_release_gil(vm, &state);
#if !defined(_WIN32) && !defined(_WIN64)
    pthread_mutex_lock(&nss_static_mutex);
#endif
    struct hostent *src = gethostbyaddr((const void *)addr, (socklen_t)len, (int)type);
    int rc = 0;
    if (!src) {
        *resultp = 0;
        if (herrp) *herrp = HOST_NOT_FOUND;
    } else {
        int naliases = nss_count_list(src->h_aliases);
        int naddrs = nss_count_list(src->h_addr_list);
        size_t need = strlen(src->h_name) + 1;
        for (int i = 0; i < naliases; i++) need += strlen(src->h_aliases[i]) + 1;
        need = nss_r_layout_size(naliases, need) +
               nss_r_layout_size(naddrs, (size_t)naddrs * (size_t)src->h_length);
        if (need > blen) {
            rc = ERANGE;
            *resultp = 0;
        } else {
            char *cursor = b;
            char *end = b + blen;
            size_t namelen = strlen(src->h_name) + 1;
            if (cursor + namelen > end) { rc = ERANGE; *resultp = 0; goto done2; }
            memcpy(cursor, src->h_name, namelen);
            out->h_name = cursor;
            cursor += namelen;

            char **aliases = nss_r_copy_ptr_array(src->h_aliases, naliases, &cursor, end);
            if (!aliases) { rc = ERANGE; *resultp = 0; goto done2; }
            out->h_aliases = aliases;

            out->h_addrtype = src->h_addrtype;
            out->h_length = src->h_length;

            char *p = cursor;
            p = (char *)(((uintptr_t)p + sizeof(char *) - 1) & ~(uintptr_t)(sizeof(char *) - 1));
            char **addrs = (char **)(void *)p;
            if ((char *)(addrs + naddrs + 1) > end) { rc = ERANGE; *resultp = 0; goto done2; }
            char *ap = (char *)(addrs + naddrs + 1);
            for (int i = 0; i < naddrs; i++) {
                if (ap + src->h_length > end) { rc = ERANGE; *resultp = 0; goto done2; }
                memcpy(ap, src->h_addr_list[i], (size_t)src->h_length);
                addrs[i] = ap;
                ap += src->h_length;
            }
            addrs[naddrs] = 0;
            out->h_addr_list = addrs;

            *resultp = out;
        }
    }
done2:
#if !defined(_WIN32) && !defined(_WIN64)
    pthread_mutex_unlock(&nss_static_mutex);
#endif
    if (gil) posix_acquire_and_restore_gil(vm, &state);
    return (long long)rc;
}

static long long wrap_getnetbyname_r_gil(long long name, long long ret, long long buf,
                                          long long buflen, long long result, long long h_errnop) {
    struct netent *out = (struct netent *)ret;
    char *b = (char *)buf;
    size_t blen = (size_t)buflen;
    struct netent **resultp = (struct netent **)result;
    int *herrp = (int *)h_errnop;

    VirtualMachine *vm = current_vm();
    ExecState state;
    int gil = vm && vm->gil_initialized;
    if (gil) posix_save_and_release_gil(vm, &state);
#if !defined(_WIN32) && !defined(_WIN64)
    pthread_mutex_lock(&nss_static_mutex);
#endif
    struct netent *src = getnetbyname((const char *)name);
    int rc = 0;
    if (!src) {
        *resultp = 0;
        if (herrp) *herrp = HOST_NOT_FOUND;
    } else {
        int naliases = nss_count_list(src->n_aliases);
        size_t need = strlen(src->n_name) + 1;
        for (int i = 0; i < naliases; i++) need += strlen(src->n_aliases[i]) + 1;
        need = nss_r_layout_size(naliases, need);
        if (need > blen) {
            rc = ERANGE;
            *resultp = 0;
        } else {
            char *cursor = b;
            char *end = b + blen;
            size_t namelen = strlen(src->n_name) + 1;
            if (cursor + namelen > end) { rc = ERANGE; *resultp = 0; goto done3; }
            memcpy(cursor, src->n_name, namelen);
            out->n_name = cursor;
            cursor += namelen;

            char **aliases = nss_r_copy_ptr_array(src->n_aliases, naliases, &cursor, end);
            if (!aliases) { rc = ERANGE; *resultp = 0; goto done3; }
            out->n_aliases = aliases;

            out->n_addrtype = src->n_addrtype;
            out->n_net = src->n_net;

            *resultp = out;
        }
    }
done3:
#if !defined(_WIN32) && !defined(_WIN64)
    pthread_mutex_unlock(&nss_static_mutex);
#endif
    if (gil) posix_acquire_and_restore_gil(vm, &state);
    return (long long)rc;
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

// statfs/fstatfs (#792): include/sys/mount.h deliberately declares a
// minimal, CCCC-canonical `struct statfs` projection -- NOT the host ABI
// layout, which is enormous (~2100 bytes on macOS, mostly mount-path
// strings and per-platform extras) versus the guest's ~56-byte struct.
// Registering the real statfs()/fstatfs() directly against a pointer to
// the guest struct would have the host write far past the end of it,
// corrupting adjacent guest memory (confirmed empirically: a canary
// written just past a malloc'd guest-sized buffer gets clobbered). So
// these translate through a host-sized local and copy only the documented
// fields across, the same host-numbering-translation shape as
// wrap_sysconf/wrap_pathconf/wrap_confstr above. Unlike struct stat
// (include/sys/stat.h, byte-for-byte host-matched with a
// _Static_assert), statfs is intentionally not host-matched -- the real
// layout carries far more than any of CCCC's stdlib needs to expose.
struct cccc_guest_statfs {
    unsigned int  f_bsize;
    unsigned int  f_iosize;
    unsigned long f_blocks;
    unsigned long f_bfree;
    unsigned long f_bavail;
    unsigned long f_files;
    unsigned long f_ffree;
    unsigned int  f_flags;
};

static void copy_statfs_fields(const struct statfs *host_buf, long long guest_ptr) {
    struct cccc_guest_statfs *g = (struct cccc_guest_statfs *)(void *)guest_ptr;
    g->f_bsize  = (unsigned int)host_buf->f_bsize;
#ifdef __linux__
    // glibc's struct statfs has f_frsize, not f_iosize; closest analog.
    g->f_iosize = (unsigned int)host_buf->f_frsize;
#else
    g->f_iosize = (unsigned int)host_buf->f_iosize;
#endif
    g->f_blocks = (unsigned long)host_buf->f_blocks;
    g->f_bfree  = (unsigned long)host_buf->f_bfree;
    g->f_bavail = (unsigned long)host_buf->f_bavail;
    g->f_files  = (unsigned long)host_buf->f_files;
    g->f_ffree  = (unsigned long)host_buf->f_ffree;
    g->f_flags  = (unsigned int)host_buf->f_flags;
}

static long long wrap_statfs(long long path, long long buf) {
    struct statfs host_buf;
    int rc = statfs((const char *)path, &host_buf);
    if (rc == 0 && buf) copy_statfs_fields(&host_buf, buf);
    return rc;
}

static long long wrap_fstatfs(long long fd, long long buf) {
    struct statfs host_buf;
    int rc = fstatfs((int)fd, &host_buf);
    if (rc == 0 && buf) copy_statfs_fields(&host_buf, buf);
    return rc;
}

// ---------------------------------------------------------------------------
// vsyslog() (#803) -- forwards a captured cccc va_list to the host's real
// variadic syslog() via ffi_prep_cif_var, same technique as
// format_printf.c's wrap_cccc_vprintf family (#407). Unlike a plain printf
// forward, syslog's "%m" conversion (strerror(errno)) consumes zero
// variadic args -- the shared cccc_parse_printf_fmt classifies unknown
// conversions as taking one INT arg, which would misalign extraction here,
// so this uses its own parser that special-cases 'm' as a no-arg literal.
// va_ffi_helper.h's __attribute__((unused)) markers rely on __attribute__
// actually expanding; internal.h (included above) #defines __attribute__(x)
// to nothing for this TU, so cccc_parse_printf_fmt/cccc_parse_scanf_fmt
// (unused here -- this file only needs cccc_va_extract/cccc_ffi_call_variadic
// and its own syslog-specific parser below) would otherwise warn.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "va_ffi_helper.h"
#pragma GCC diagnostic pop

static int cccc_parse_syslog_fmt(const char *fmt, int *types, int max_args) {
    int n = 0;
    for (const char *p = fmt; *p; p++) {
        if (*p != '%')
            continue;
        p++;
        if (!*p || *p == '%' || *p == 'm')
            continue;

        while (*p == '-' || *p == '+' || *p == ' ' || *p == '#' || *p == '0')
            p++;
        if (!*p) break;

        if (*p == '*') {
            if (n < max_args) types[n++] = CCCC_VAARG_INT;
            p++;
        } else {
            while (*p >= '0' && *p <= '9') p++;
        }
        if (!*p) break;

        if (*p == '.') {
            p++;
            if (*p == '*') {
                if (n < max_args) types[n++] = CCCC_VAARG_INT;
                p++;
            } else {
                while (*p >= '0' && *p <= '9') p++;
            }
        }
        if (!*p) break;

        while (*p == 'h' || *p == 'l' || *p == 'j' || *p == 'z' ||
               *p == 't' || *p == 'L')
            p++;
        if (!*p) break;

        if (n < max_args) {
            switch (*p) {
            case 'f': case 'F': case 'e': case 'E':
            case 'g': case 'G': case 'a': case 'A':
                types[n++] = CCCC_VAARG_DOUBLE;
                break;
            default:
                types[n++] = CCCC_VAARG_INT;
                break;
            }
        }
    }
    return n;
}

static long long wrap_vsyslog(long long priority, long long fmt, long long va_ptr) {
    cccc_va_list_t *va = (cccc_va_list_t *)va_ptr;
    int types[CCCC_VA_MAX_ARGS];
    int n = cccc_parse_syslog_fmt((const char *)fmt, types, CCCC_VA_MAX_ARGS);
    int64_t vals[CCCC_VA_MAX_ARGS];
    cccc_va_extract(va, types, n, vals);
    int64_t fixed[] = { (int64_t)priority, (int64_t)fmt };
    return cccc_ffi_call_variadic((void *)syslog, 2, fixed, n, types, vals);
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
    cc_register_cfunc(vm, "select",  (void*)wrap_select_gil,  5, 0);
    cc_register_cfunc(vm, "pselect", (void*)wrap_pselect_gil, 6, 0);
    cc_register_cfunc(vm, "accept",  (void*)wrap_accept_gil,  3, 0);
    cc_register_cfunc(vm, "connect", (void*)wrap_connect_gil, 3, 0);
    cc_register_cfunc(vm, "recv",     (void*)wrap_recv_gil,     4, 0);
    cc_register_cfunc(vm, "send",     (void*)wrap_send_gil,     4, 0);
    cc_register_cfunc(vm, "recvfrom", (void*)wrap_recvfrom_gil, 6, 0);
    cc_register_cfunc(vm, "sendto",   (void*)wrap_sendto_gil,   6, 0);
    cc_register_cfunc(vm, "sendmsg",  (void*)wrap_sendmsg_gil,  3, 0);
    cc_register_cfunc(vm, "recvmsg",  (void*)wrap_recvmsg_gil,  3, 0);
    cc_register_cfunc(vm, "wait",    (void*)wrap_wait_gil,    1, 0);
    cc_register_cfunc(vm, "waitpid", (void*)wrap_waitpid_gil, 3, 0);
    cc_register_cfunc(vm, "waitid",  (void*)wrap_waitid_gil,  4, 0);
    cc_register_cfunc(vm, "wait3",   (void*)wrap_wait3_gil,   3, 0);
    cc_register_cfunc(vm, "wait4",   (void*)wrap_wait4_gil,   4, 0);
    cc_register_cfunc(vm, "sleep",   (void*)wrap_sleep_gil,   1, 0);
    cc_register_cfunc(vm, "usleep",  (void*)wrap_usleep_gil,  1, 0);
    cc_register_cfunc(vm, "nanosleep",(void*)wrap_nanosleep_gil, 2, 0);
    cc_register_cfunc(vm, "pause",   (void*)wrap_pause_gil,   0, 0);
    // DNS/NSS lookups (#748) — real resolver calls can block for seconds
    cc_register_cfunc(vm, "gethostbyname",(void*)wrap_gethostbyname_gil, 1, 0);
    cc_register_cfunc(vm, "gethostbyaddr",(void*)wrap_gethostbyaddr_gil, 3, 0);
    cc_register_cfunc(vm, "getaddrinfo", (void*)wrap_getaddrinfo_gil,    4, 0);
    cc_register_cfunc(vm, "getnameinfo", (void*)wrap_getnameinfo_gil,    7, 0);
    cc_register_cfunc(vm, "getnetbyname",(void*)wrap_getnetbyname_gil,   1, 0);
    cc_register_cfunc(vm, "getnetbyaddr",(void*)wrap_getnetbyaddr_gil,   2, 0);
    // Race-free _r variants (#785)
    cc_register_cfunc(vm, "gethostbyname_r",(void*)wrap_gethostbyname_r_gil, 6, 0);
    cc_register_cfunc(vm, "gethostbyaddr_r",(void*)wrap_gethostbyaddr_r_gil, 8, 0);
    cc_register_cfunc(vm, "getnetbyname_r", (void*)wrap_getnetbyname_r_gil,  6, 0);

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
#ifdef __linux__
    // fdatasync: POSIX, but absent from Darwin's libc entirely (macOS has
    // no equivalent syscall/libc symbol at all, unlike fsync -- the
    // closest analog is the fcntl F_FULLFSYNC command, not a drop-in
    // replacement). Guest-side declaration in include/unistd.h is
    // __linux__-guarded to match (#783).
    cc_register_cfunc(vm, "fdatasync",(void*)fdatasync,1, 0);
#endif
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
    cc_register_cfunc(vm, "chown",    (void*)chown,    3, 0); // #783: declared, never registered
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
    // flock/ioctl/statfs/fstatfs: declared in include/ but never registered
    // anywhere -- same "undefined function" gap class as #783, surfaced by
    // widening tools/audit_ffi.py's header scan for #784/#792.
    cc_register_cfunc(vm, "flock",   (void*)flock,   2, 0);
    // ioctl is a general passthrough (like the real syscall) -- only the
    // two request codes include/sys/ioctl.h declares, TIOCGWINSZ/
    // TIOCSWINSZ, have a verified guest/host struct winsize layout match
    // (both 8 bytes: 4x unsigned short). Any other request code risks the
    // same guest/host struct-size mismatch that statfs had (see
    // wrap_statfs below) -- that's inherent to ioctl's design, not
    // something this registration can guard against generically.
    cc_register_variadic_cfunc(vm, "ioctl", (void*)ioctl, 2, 0);
    cc_register_cfunc(vm, "statfs",  (void*)wrap_statfs,  2, 0);
    cc_register_cfunc(vm, "fstatfs", (void*)wrap_fstatfs, 2, 0);

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
    cc_register_cfunc(vm, "symlink", (void*)symlink, 2, 0); // #783: declared, never registered
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
    cc_register_cfunc(vm, "freeaddrinfo",(void*)wrap_freeaddrinfo, 1, 0);
    cc_register_cfunc(vm, "setnetent",   (void*)setnetent,      1, 0);
    cc_register_cfunc(vm, "endnetent",   (void*)endnetent,      0, 0);
    // servent/protoent (#746) -- local /etc/services /etc/protocols lookups,
    // fast and CPU-bound like getnetbyname/getnetbyaddr; no GIL release needed
    cc_register_cfunc(vm, "getservbyname",(void*)getservbyname,  2, 0);
    cc_register_cfunc(vm, "getservbyport",(void*)getservbyport, 2, 0);
    cc_register_cfunc(vm, "setservent",   (void*)setservent,    1, 0);
    cc_register_cfunc(vm, "endservent",   (void*)endservent,    0, 0);
    cc_register_cfunc(vm, "getprotobyname",  (void*)getprotobyname,   1, 0);
    cc_register_cfunc(vm, "getprotobynumber",(void*)getprotobynumber,1, 0);
    cc_register_cfunc(vm, "setprotoent",     (void*)setprotoent,     1, 0);
    cc_register_cfunc(vm, "endprotoent",     (void*)endprotoent,     0, 0);
    cc_register_cfunc(vm, "getrusage",       (void*)getrusage,       2, 0);

    // struct rlimit / RLIMIT_* / getpriority/setpriority (#786) -- fast,
    // local, no GIL release needed.
    cc_register_cfunc(vm, "getrlimit",   (void*)getrlimit,   2, 0);
    cc_register_cfunc(vm, "setrlimit",   (void*)setrlimit,   2, 0);
    cc_register_cfunc(vm, "getpriority", (void*)getpriority, 2, 0);
    cc_register_cfunc(vm, "setpriority", (void*)setpriority, 3, 0);

    // sys/utsname.h uname() and sys/times.h times() (#733/#737) -- fast,
    // local, no GIL release needed.
    cc_register_cfunc(vm, "uname", (void*)uname, 1, 0);
    cc_register_cfunc(vm, "times", (void*)times, 1, 0);

    // syslog.h (#803) -- LOG_* constants are identical on both platforms so
    // the header needs no guards. syslog() is registered as a real variadic
    // FFI function (not a va_list-forwarding wrapper): codegen computes
    // double_arg_mask per call-site from the caller's static argument types
    // (src/codegen.c:5405-5415), so this correctly threads through %f
    // arguments the same way a direct printf() call does -- no format-string
    // parsing required here, unlike the vprintf-family wrappers, which only
    // exist because a captured va_list has already erased those static types.
    cc_register_cfunc(vm, "openlog",    (void*)openlog,    3, 0);
    cc_register_cfunc(vm, "closelog",   (void*)closelog,   0, 0);
    cc_register_cfunc(vm, "setlogmask", (void*)setlogmask, 1, 0);
    cc_register_variadic_cfunc(vm, "syslog", (void*)syslog, 2, 0);
    cc_register_cfunc(vm, "vsyslog",    (void*)wrap_vsyslog, 3, 0);

    // net/if.h (#788) -- interface name<->index resolution, needed to target
    // a specific interface (e.g. loopback) for IPV6_MULTICAST_IF/JOIN_GROUP
    // instead of relying on index 0. Fast/local, no GIL release needed.
    cc_register_cfunc(vm, "if_nametoindex",  (void*)if_nametoindex,  1, 0);
    cc_register_cfunc(vm, "if_indextoname",  (void*)if_indextoname,  2, 0);
    cc_register_cfunc(vm, "if_nameindex",    (void*)if_nameindex,    0, 0);
    cc_register_cfunc(vm, "if_freenameindex",(void*)if_freenameindex,1, 0);

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
