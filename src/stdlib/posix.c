// POSIX and dlfcn stdlib function registration
#include "../cccc.h"
#include "../internal.h"

#if !defined(_WIN32) && !defined(_WIN64)
// Must be defined before <netinet/in.h> pulls in the real system header, or
// the advanced IPV6_* options (#749) -- IPV6_PKTINFO, IPV6_TCLASS, etc. --
// won't be visible on macOS (RFC 3542 options are opt-in there; Linux glibc
// exposes them unconditionally).
#define __APPLE_USE_RFC_3542
#include <aio.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <fts.h>
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
#include <iconv.h>
#include <langinfo.h>
#include <locale.h>
#ifdef __APPLE__
#include <xlocale.h>
#endif
#include <monetary.h>
#include <search.h>
#include <nl_types.h>
#include <sched.h>
#include <spawn.h>
#ifdef __linux__
// SCHED_BATCH/SCHED_IDLE are glibc extensions gated behind __USE_GNU, which
// the host <sched.h> only exposes under _GNU_SOURCE -- same gap class as
// mremap below, so defined locally with their known glibc values rather
// than flipping on _GNU_SOURCE for the whole TU.
#ifndef SCHED_BATCH
#define SCHED_BATCH 3
#endif
#ifndef SCHED_IDLE
#define SCHED_IDLE 5
#endif
#endif
#include <signal.h>
#include <string.h>
#include <strings.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/ipc.h>
#ifdef __linux__
// IPC_INFO is a glibc extension gated behind __USE_GNU, which the host
// <sys/ipc.h> only exposes under _GNU_SOURCE -- same gap class as
// SCHED_BATCH/SCHED_IDLE above and mremap below, so defined locally with
// its known glibc value rather than flipping on _GNU_SOURCE for the whole
// TU.
#ifndef IPC_INFO
#define IPC_INFO 3
#endif
#endif
#include <sys/mman.h>
#include <sys/msg.h>
#include <sys/resource.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/time.h>
#include <sys/times.h>
#include <sys/uio.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <syslog.h>
#include <termios.h>
#include <unistd.h>
#include <utime.h>
#include <wordexp.h>
#ifdef __linux__
#include <sys/vfs.h>
// mqueue.h (#805) -- Linux-only, no macOS equivalent at all (see
// include/mqueue.h). aio_read/aio_write/aio_error/aio_return/aio_cancel/
// aio_suspend/aio_fsync/lio_listio (#804) and mq_open/mq_send/mq_receive/
// mq_timedsend/mq_timedreceive/mq_notify/mq_setattr/mq_getattr all resolve
// straight out of libc.so.6 with no extra link flag needed -- glibc >=
// 2.34 merged librt's symbols into libc itself, verified by linking
// without -lrt in both Linux containers (x86_64 and aarch64).
#include <mqueue.h>
#else
#include <sys/mount.h>
#include <sys/param.h>
#endif
#if defined(__APPLE__) || defined(CCCC_HAS_NDBM)
// ndbm.h (#810, #871) -- macOS/BSD natively; on Linux only when built with
// CCCC_HAS_NDBM=1 against libgdbm-compat (see include/ndbm.h). Guarded
// independently of the __linux__ split above, not folded into either arm
// of it, since Linux can go either way depending on the build knob.
#include <ndbm.h>
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

// readv/writev -- scatter/gather I/O at the fd's own file position.
// Blocking, like read/write above, so they release the GIL the same way.
// struct iovec needs no guest/host translation (void* + size_t, identical
// on both hosts), so unlike ioctl below there is no layout risk here.
static long long wrap_readv_gil(long long fd, long long iov, long long iovcnt) {
    VirtualMachine *vm = current_vm();
    if (!vm || !vm->gil_initialized)
        return (long long)readv((int)fd, (const struct iovec *)iov, (int)iovcnt);
    ExecState state;
    posix_save_and_release_gil(vm, &state);
    ssize_t r = readv((int)fd, (const struct iovec *)iov, (int)iovcnt);
    posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

static long long wrap_writev_gil(long long fd, long long iov, long long iovcnt) {
    VirtualMachine *vm = current_vm();
    if (!vm || !vm->gil_initialized)
        return (long long)writev((int)fd, (const struct iovec *)iov, (int)iovcnt);
    ExecState state;
    posix_save_and_release_gil(vm, &state);
    ssize_t r = writev((int)fd, (const struct iovec *)iov, (int)iovcnt);
    posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

// preadv/pwritev (#793) -- the readv/writev analogs of pread/pwrite:
// scatter/gather I/O at an explicit offset. Blocking, like pread/pwrite
// above, so they release the GIL the same way. struct iovec needs no
// guest/host translation (void* + size_t, identical on both hosts), so
// unlike ioctl below there is no layout risk here.
static long long wrap_preadv_gil(long long fd, long long iov, long long iovcnt, long long offset) {
    VirtualMachine *vm = current_vm();
    if (!vm || !vm->gil_initialized)
        return (long long)preadv((int)fd, (const struct iovec *)iov, (int)iovcnt, (off_t)offset);
    ExecState state;
    posix_save_and_release_gil(vm, &state);
    ssize_t r = preadv((int)fd, (const struct iovec *)iov, (int)iovcnt, (off_t)offset);
    posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

static long long wrap_pwritev_gil(long long fd, long long iov, long long iovcnt, long long offset) {
    VirtualMachine *vm = current_vm();
    if (!vm || !vm->gil_initialized)
        return (long long)pwritev((int)fd, (const struct iovec *)iov, (int)iovcnt, (off_t)offset);
    ExecState state;
    posix_save_and_release_gil(vm, &state);
    ssize_t r = pwritev((int)fd, (const struct iovec *)iov, (int)iovcnt, (off_t)offset);
    posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

#ifdef __linux__
// preadv2/pwritev2 (#793) -- same as preadv/pwritev plus an RWF_* flags
// word; Linux-only syscalls (glibc >= 2.26), same gap class as
// mremap/fallocate/splice registered further down. Forward-declared
// locally for the same reason (host <sys/uio.h> gates these behind
// _GNU_SOURCE; glibc exports them regardless).
extern ssize_t preadv2(int fd, const struct iovec *iov, int iovcnt, off_t offset, int flags);
extern ssize_t pwritev2(int fd, const struct iovec *iov, int iovcnt, off_t offset, int flags);

static long long wrap_preadv2_gil(long long fd, long long iov, long long iovcnt,
                                   long long offset, long long flags) {
    VirtualMachine *vm = current_vm();
    if (!vm || !vm->gil_initialized)
        return (long long)preadv2((int)fd, (const struct iovec *)iov, (int)iovcnt,
                                  (off_t)offset, (int)flags);
    ExecState state;
    posix_save_and_release_gil(vm, &state);
    ssize_t r = preadv2((int)fd, (const struct iovec *)iov, (int)iovcnt, (off_t)offset, (int)flags);
    posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

static long long wrap_pwritev2_gil(long long fd, long long iov, long long iovcnt,
                                    long long offset, long long flags) {
    VirtualMachine *vm = current_vm();
    if (!vm || !vm->gil_initialized)
        return (long long)pwritev2((int)fd, (const struct iovec *)iov, (int)iovcnt,
                                   (off_t)offset, (int)flags);
    ExecState state;
    posix_save_and_release_gil(vm, &state);
    ssize_t r = pwritev2((int)fd, (const struct iovec *)iov, (int)iovcnt, (off_t)offset, (int)flags);
    posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}
#endif

// poll.h (#821) -- POLLRDNORM/POLLRDBAND share the same bit values on both
// hosts, but POLLWRNORM/POLLWRBAND diverge (macOS aliases POLLWRNORM to
// POLLOUT and uses 0x0100 for POLLWRBAND; glibc uses 0x0100/0x0200). CCCC's
// guest-visible bits (include/poll.h) use canonical numbering equal to
// glibc's, so translation is a no-op on Linux; on macOS it maps the two
// divergent bits to the host's real values, the same pattern wrap_setlocale
// uses for LC_* categories.
static short guest_to_host_pollev(short guest_events) {
#ifdef __APPLE__
    short host = guest_events & (short)~(0x0040 | 0x0080 | 0x0100 | 0x0200);
    if (guest_events & 0x0040) host |= POLLRDNORM;
    if (guest_events & 0x0080) host |= POLLRDBAND;
    if (guest_events & 0x0100) host |= POLLWRNORM;
    if (guest_events & 0x0200) host |= POLLWRBAND;
    return host;
#else
    return guest_events;
#endif
}

static short host_to_guest_pollev(short host_revents) {
#ifdef __APPLE__
    short guest = host_revents & (short)~(POLLRDNORM | POLLRDBAND | POLLWRNORM | POLLWRBAND);
    if (host_revents & POLLRDNORM) guest |= 0x0040;
    if (host_revents & POLLRDBAND) guest |= 0x0080;
    // POLLWRNORM aliases POLLOUT (0x0004) on macOS, so a host POLLOUT bit
    // sets both the canonical POLLOUT and POLLWRNORM guest bits -- this is
    // intentional and documented, not a bug: a caller polling for
    // POLLWRNORM alone still observes it set when writable.
    if (host_revents & POLLWRNORM) guest |= 0x0100;
    if (host_revents & POLLWRBAND) guest |= 0x0200;
    return guest;
#else
    return host_revents;
#endif
}

// Marshals a guest struct pollfd[] through a host-side temp array so event
// bit translation never mutates guest memory in place while the GIL is
// released (another guest thread could observe a half-translated array).
// struct pollfd's layout (int, short, short) is identical on both hosts --
// only the bit values need translating.
#define POLL_STACK_NFDS 32

static int poll_marshal_in(struct pollfd *guest_fds, nfds_t nfds,
                           struct pollfd *stack_buf, struct pollfd **out_host) {
    struct pollfd *host = (nfds <= POLL_STACK_NFDS) ? stack_buf
                        : (struct pollfd *)malloc(sizeof(struct pollfd) * nfds);
    if (!host) {
        errno = ENOMEM;
        return -1;
    }
    for (nfds_t i = 0; i < nfds; i++) {
        host[i].fd = guest_fds[i].fd;
        host[i].events = guest_to_host_pollev(guest_fds[i].events);
        host[i].revents = 0;
    }
    *out_host = host;
    return 0;
}

static void poll_marshal_out(struct pollfd *host, struct pollfd *guest_fds, nfds_t nfds) {
    for (nfds_t i = 0; i < nfds; i++)
        guest_fds[i].revents = host_to_guest_pollev(host[i].revents);
}

static long long wrap_poll_gil(long long fds, long long nfds, long long timeout) {
    struct pollfd stack_buf[POLL_STACK_NFDS];
    struct pollfd *host_fds;
    if (poll_marshal_in((struct pollfd *)fds, (nfds_t)nfds, stack_buf, &host_fds) < 0)
        return -1;

    VirtualMachine *vm = current_vm();
    int r;
    if (!vm || !vm->gil_initialized) {
        r = poll(host_fds, (nfds_t)nfds, (int)timeout);
    } else {
        ExecState state;
        posix_save_and_release_gil(vm, &state);
        r = poll(host_fds, (nfds_t)nfds, (int)timeout);
        posix_acquire_and_restore_gil(vm, &state);
    }

    poll_marshal_out(host_fds, (struct pollfd *)fds, (nfds_t)nfds);
    if (host_fds != stack_buf) free(host_fds);
    return (long long)r;
}

#ifndef __linux__
// macOS ppoll() emulation helper -- swap in the translated sigmask via
// pthread_sigmask(), poll(), then restore the previous mask, preserving
// errno across the restore call. Not atomic (see wrap_ppoll_gil's comment
// for the race this leaves open); factored out as a real function rather
// than a nested one since nested functions are a GCC-only extension clang
// doesn't support.
static int ppoll_emulate_macos(struct pollfd *host_fds, nfds_t nfds, int ms,
                               const sigset_t *host_set_ptr) {
    sigset_t old_set;
    int have_old = 0;
    if (host_set_ptr) {
        if (pthread_sigmask(SIG_SETMASK, host_set_ptr, &old_set) == 0)
            have_old = 1;
    }
    int r = poll(host_fds, nfds, ms);
    int saved_errno = errno;
    if (have_old)
        pthread_sigmask(SIG_SETMASK, &old_set, NULL);
    errno = saved_errno;
    return r;
}
#endif

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

// ppoll() (#821) -- the poll()-with-timeout-and-sigmask analog of
// pselect(). The guest sigset_t is translated to a real host sigset_t via
// the same guest_sigset_to_host() pselect() uses above.
//
// macOS has no native ppoll(); it is emulated via pthread_sigmask()+poll()
// (ppoll_emulate_macos above), which is NOT atomic like the real
// ppoll()/pselect() -- a signal delivered between the mask swap and
// poll()'s internal wait is not guaranteed to interrupt it, which is
// exactly the race the real syscall exists to close (#824). Because this
// is a lossy emulation of a primitive the host doesn't actually have, it
// is only registered/declared under --posix-emulation (see poll.h and
// register_posix_functions below) -- without the flag, ppoll() is simply
// undeclared on macOS, matching what a native compiler on the same host
// would do.
static long long wrap_ppoll_gil(long long fds, long long nfds, long long timeout,
                                long long sigmask) {
    struct pollfd stack_buf[POLL_STACK_NFDS];
    struct pollfd *host_fds;
    if (poll_marshal_in((struct pollfd *)fds, (nfds_t)nfds, stack_buf, &host_fds) < 0)
        return -1;

    sigset_t host_set;
    sigset_t *host_set_ptr = NULL;
    if (sigmask) {
        guest_sigset_to_host(*(unsigned int *)(void *)sigmask, &host_set);
        host_set_ptr = &host_set;
    }

    VirtualMachine *vm = current_vm();
    int r;
#ifdef __linux__
    // ppoll is a glibc extension gated behind __USE_GNU, which the host
    // <poll.h>/<sys/poll.h> only exposes under _GNU_SOURCE -- same gap
    // class as mremap/fallocate/splice above, so forward-declared locally
    // rather than flipping on _GNU_SOURCE for the whole TU. glibc still
    // exports the symbol regardless.
    extern int ppoll(struct pollfd *fds, nfds_t nfds,
                     const struct timespec *timeout, const sigset_t *sigmask);
    if (!vm || !vm->gil_initialized) {
        r = ppoll(host_fds, (nfds_t)nfds, (const struct timespec *)timeout, host_set_ptr);
    } else {
        ExecState state;
        posix_save_and_release_gil(vm, &state);
        r = ppoll(host_fds, (nfds_t)nfds, (const struct timespec *)timeout, host_set_ptr);
        posix_acquire_and_restore_gil(vm, &state);
    }
#else
    // Convert struct timespec -> milliseconds for poll(); NULL means block
    // forever (INFTIM/-1). Sub-millisecond remainders round up so a
    // nonzero timeout never collapses into a busy-poll.
    int ms;
    if (!timeout) {
        ms = -1;
    } else {
        const struct timespec *ts = (const struct timespec *)timeout;
        long long total_ns = (long long)ts->tv_sec * 1000000000LL + ts->tv_nsec;
        ms = (int)((total_ns + 999999) / 1000000);
        if (ms < 0) ms = 0;
    }

    if (!vm || !vm->gil_initialized) {
        r = ppoll_emulate_macos(host_fds, (nfds_t)nfds, ms, host_set_ptr);
    } else {
        ExecState state;
        posix_save_and_release_gil(vm, &state);
        r = ppoll_emulate_macos(host_fds, (nfds_t)nfds, ms, host_set_ptr);
        posix_acquire_and_restore_gil(vm, &state);
    }
#endif

    poll_marshal_out(host_fds, (struct pollfd *)fds, (nfds_t)nfds);
    if (host_fds != stack_buf) free(host_fds);
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
// gethostbyname_r/gethostbyaddr_r/getnetbyname_r *portable shim* below, so
// the static buffer is at least never *written* concurrently -- the plain
// functions' returned pointer is still only valid until the next call from
// any thread on any of these six functions, which the mutex cannot fix;
// that's what the _r variants are for. On Linux the _r functions instead
// forward straight to glibc's own native _r variants (#791), which touch no
// static storage at all and therefore never take this mutex.
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
//
// This shim (and its nss_r_layout_size/nss_r_copy_ptr_array/nss_count_list
// helpers) is only compiled where it is actually used -- everywhere except
// Linux, which forwards to glibc's native _r functions instead (#791,
// wrap_gethostbyname_r_gil etc. below) and would otherwise leave these as
// unused statics.
#ifndef __linux__

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

static long long nss_gethostbyname_r_shim(long long name, long long ret, long long buf,
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

static long long nss_gethostbyaddr_r_shim(long long addr, long long len, long long type, long long ret,
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

static long long nss_getnetbyname_r_shim(long long name, long long ret, long long buf,
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

#endif /* !__linux__ */

// ---------------------------------------------------------------------------
// gethostbyname_r/gethostbyaddr_r/getnetbyname_r dispatch (#791): on Linux,
// forward straight to glibc's own native _r variants instead of the
// portable shim above. glibc's _r functions use no static/shared storage at
// all (that's the entire point of the _r family), so this path deliberately
// takes no lock -- unlike the shim, which serializes every guest thread's
// lookup through nss_static_mutex. Guest-visible signature/behavior (return
// value, *result, *h_errnop, ERANGE on a too-small buffer) is unchanged;
// this is a Linux-only implementation swap for less contention, not an API
// change (see the deferred-from-#785 followup that filed this). Forward-
// declared locally rather than flipping on _GNU_SOURCE for the whole TU --
// same gap class as mremap/fallocate/splice/ppoll above -- glibc exports
// these regardless of feature-test macros. macOS and every other platform
// keep using the portable shim, since they have no native _r variants at
// all (glibc-only extensions).
//
// One deliberate behavior refinement on the Linux path: on a not-found
// lookup, the portable shim above always sets *h_errnop = HOST_NOT_FOUND,
// whereas glibc's native _r functions set *h_errnop to whichever of
// HOST_NOT_FOUND/TRY_AGAIN/NO_RECOVERY/NO_DATA actually applies -- strictly
// more accurate, not a regression (the shim's HOST_NOT_FOUND-only behavior
// was never a documented guarantee, just what the shim happened to do).
#ifdef __linux__
extern int gethostbyname_r(const char *name, struct hostent *ret,
                           char *buf, size_t buflen,
                           struct hostent **result, int *h_errnop);
extern int gethostbyaddr_r(const void *addr, socklen_t len, int type,
                           struct hostent *ret, char *buf, size_t buflen,
                           struct hostent **result, int *h_errnop);
extern int getnetbyname_r(const char *name, struct netent *ret,
                          char *buf, size_t buflen,
                          struct netent **result, int *h_errnop);
#endif

static long long wrap_gethostbyname_r_gil(long long name, long long ret, long long buf,
                                           long long buflen, long long result, long long h_errnop) {
#ifdef __linux__
    VirtualMachine *vm = current_vm();
    ExecState state;
    int gil = vm && vm->gil_initialized;
    if (gil) posix_save_and_release_gil(vm, &state);
    int rc = gethostbyname_r((const char *)name, (struct hostent *)ret, (char *)buf,
                             (size_t)buflen, (struct hostent **)result, (int *)h_errnop);
    if (gil) posix_acquire_and_restore_gil(vm, &state);
    return (long long)rc;
#else
    return nss_gethostbyname_r_shim(name, ret, buf, buflen, result, h_errnop);
#endif
}

static long long wrap_gethostbyaddr_r_gil(long long addr, long long len, long long type, long long ret,
                                           long long buf, long long buflen, long long result, long long h_errnop) {
#ifdef __linux__
    VirtualMachine *vm = current_vm();
    ExecState state;
    int gil = vm && vm->gil_initialized;
    if (gil) posix_save_and_release_gil(vm, &state);
    int rc = gethostbyaddr_r((const void *)addr, (socklen_t)len, (int)type,
                             (struct hostent *)ret, (char *)buf, (size_t)buflen,
                             (struct hostent **)result, (int *)h_errnop);
    if (gil) posix_acquire_and_restore_gil(vm, &state);
    return (long long)rc;
#else
    return nss_gethostbyaddr_r_shim(addr, len, type, ret, buf, buflen, result, h_errnop);
#endif
}

static long long wrap_getnetbyname_r_gil(long long name, long long ret, long long buf,
                                          long long buflen, long long result, long long h_errnop) {
#ifdef __linux__
    VirtualMachine *vm = current_vm();
    ExecState state;
    int gil = vm && vm->gil_initialized;
    if (gil) posix_save_and_release_gil(vm, &state);
    int rc = getnetbyname_r((const char *)name, (struct netent *)ret, (char *)buf,
                            (size_t)buflen, (struct netent **)result, (int *)h_errnop);
    if (gil) posix_acquire_and_restore_gil(vm, &state);
    return (long long)rc;
#else
    return nss_getnetbyname_r_shim(name, ret, buf, buflen, result, h_errnop);
#endif
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

    VirtualMachine *vm = current_vm();
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

// fts.h (#811) -- fts_open()'s comparator takes the same
// int (*)(const FTSENT **, const FTSENT **) shape as scandir()'s compar
// above, so it gets the same thread-local-slot + trampoline treatment for
// the duration of any single host call. Unlike scandir()/glob(), though,
// the comparator isn't only invoked inside fts_open() itself -- libc
// retains it on the FTS handle and calls it again from every fts_read()
// (sorting each directory's children as it descends), so the slot has to
// be re-armed on every wrapper call that might invoke it, not just at
// fts_open() time (#878: fts_read() was calling the trampoline with the
// slot back at its default 0 value, which cccc_call_guest_callback
// resolved as a jump to byte offset 0 -- the unknown-opcode trap at the
// top of the text segment -- so the comparator silently never ran).
//
// The handle -> comparator binding lives in a small fixed-size table
// (guarded by the GIL, not thread-local -- one VM, one GIL, no need for
// per-thread copies) keyed on the FTS* pointer, populated by wrap_fts_open
// on success and cleared by wrap_fts_close.
//
// fts_open()/fts_set()/fts_close() don't block meaningfully (fts_open()
// itself may stat the root paths, but that's a single bounded syscall, not
// an unbounded wait) so they keep the GIL, matching opendir()'s category
// above. fts_read()/fts_children() do real directory I/O and normally
// release the GIL like the other blocking wrappers -- but only when the
// handle has no guest comparator: a comparator firing mid-fts_read() needs
// cccc_call_guest_callback to reenter vm_eval on this same thread, which
// requires the GIL, and would otherwise race the ExecState snapshot that
// posix_save_and_release_gil parked for restoration. So a handle with a
// comparator holds the GIL across fts_read()/fts_children() for its whole
// traversal instead.
static _Thread_local long long g_fts_compar_value;
static _Thread_local int g_fts_faulted;

#define CCCC_FTS_HANDLE_MAX 16
typedef struct {
    FTS *handle;
    long long compar;
} FtsHandleBinding;
static FtsHandleBinding g_fts_handles[CCCC_FTS_HANDLE_MAX];

static long long fts_handle_compar(FTS *f) {
    for (int i = 0; i < CCCC_FTS_HANDLE_MAX; i++)
        if (g_fts_handles[i].handle == f)
            return g_fts_handles[i].compar;
    return 0;
}

static void fts_handle_bind(FTS *f, long long compar) {
    for (int i = 0; i < CCCC_FTS_HANDLE_MAX; i++) {
        if (!g_fts_handles[i].handle) {
            g_fts_handles[i].handle = f;
            g_fts_handles[i].compar = compar;
            return;
        }
    }
    // Table full: comparator won't be retained past fts_open(). Rare
    // (16 concurrent guest traversals) and fails safe -- unsorted
    // children rather than a crash.
}

static void fts_handle_unbind(FTS *f) {
    for (int i = 0; i < CCCC_FTS_HANDLE_MAX; i++) {
        if (g_fts_handles[i].handle == f) {
            g_fts_handles[i].handle = NULL;
            g_fts_handles[i].compar = 0;
            return;
        }
    }
}

static int fts_compar_trampoline(const FTSENT **a, const FTSENT **b) {
    VirtualMachine *vm = cccc_current_ffi_vm();
    long long args[2] = { (long long)(intptr_t)a, (long long)(intptr_t)b };
    long long result = 0;
    if (!vm || cccc_call_guest_callback(vm, g_fts_compar_value, args, 2, &result) != 0) {
        g_fts_faulted = 1;
        return 0;
    }
    return (int)result;
}

static long long wrap_fts_open(long long path_argv, long long options, long long compar) {
    long long saved_compar = g_fts_compar_value;
    int saved_faulted = g_fts_faulted;
    g_fts_compar_value = compar;
    g_fts_faulted = 0;

    FTS *f = fts_open((char *const *)path_argv, (int)options,
                      compar ? fts_compar_trampoline : NULL);

    int faulted = g_fts_faulted;
    g_fts_compar_value = saved_compar;
    g_fts_faulted = saved_faulted;
    if (faulted)
        errno = EFAULT;
    if (f && compar)
        fts_handle_bind(f, compar);
    return (long long)(intptr_t)f;
}

static long long wrap_fts_read(long long ftsp) {
    FTS *f = (FTS *)(intptr_t)ftsp;
    long long compar = fts_handle_compar(f);
    VirtualMachine *vm = current_vm();

    if (!compar || !vm || !vm->gil_initialized) {
        // No guest comparator bound: safe to release the GIL for the
        // blocking directory I/O, same as before.
        if (!vm || !vm->gil_initialized)
            return (long long)(intptr_t)fts_read(f);
        ExecState state;
        posix_save_and_release_gil(vm, &state);
        FTSENT *e = fts_read(f);
        posix_acquire_and_restore_gil(vm, &state);
        return (long long)(intptr_t)e;
    }

    // A comparator is bound: keep the GIL held (the trampoline reenters
    // vm_eval on this thread) and re-arm the thread-local slot, since
    // wrap_fts_open's own set/restore only covered its own call.
    long long saved_compar = g_fts_compar_value;
    int saved_faulted = g_fts_faulted;
    g_fts_compar_value = compar;
    g_fts_faulted = 0;

    FTSENT *e = fts_read(f);

    int faulted = g_fts_faulted;
    g_fts_compar_value = saved_compar;
    g_fts_faulted = saved_faulted;
    if (faulted)
        errno = EFAULT;
    return (long long)(intptr_t)e;
}

static long long wrap_fts_children(long long ftsp, long long options) {
    FTS *f = (FTS *)(intptr_t)ftsp;
    long long compar = fts_handle_compar(f);
    VirtualMachine *vm = current_vm();

    if (!compar || !vm || !vm->gil_initialized) {
        if (!vm || !vm->gil_initialized)
            return (long long)(intptr_t)fts_children(f, (int)options);
        ExecState state;
        posix_save_and_release_gil(vm, &state);
        FTSENT *e = fts_children(f, (int)options);
        posix_acquire_and_restore_gil(vm, &state);
        return (long long)(intptr_t)e;
    }

    long long saved_compar = g_fts_compar_value;
    int saved_faulted = g_fts_faulted;
    g_fts_compar_value = compar;
    g_fts_faulted = 0;

    FTSENT *e = fts_children(f, (int)options);

    int faulted = g_fts_faulted;
    g_fts_compar_value = saved_compar;
    g_fts_faulted = saved_faulted;
    if (faulted)
        errno = EFAULT;
    return (long long)(intptr_t)e;
}

static long long wrap_fts_set(long long ftsp, long long f, long long instr) {
    return (long long)fts_set((FTS *)(intptr_t)ftsp, (FTSENT *)(intptr_t)f, (int)instr);
}

static long long wrap_fts_close(long long ftsp) {
    FTS *f = (FTS *)(intptr_t)ftsp;
    fts_handle_unbind(f);
    return (long long)fts_close(f);
}

// search.h (#809) -- tsearch/tfind/tdelete/lfind/lsearch all take the same
// int (*)(const void *, const void *) comparator shape, so they share one
// trampoline + thread-local slot (each wrapper saves/restores around its
// own call, so there's no cross-call interference). twalk's
// void (*)(const void *, VISIT, int) action gets its own trampoline.
//
// hsearch() takes ENTRY (a 2-pointer struct) by value; CCCC marshals a
// guest struct-by-value FFI argument as a pointer to a caller-side
// scratch copy (the same convention #714 established for vector args,
// confirmed here empirically), so wrap_hsearch's first parameter is a
// pointer to that copy, which it dereferences to build a real host ENTRY
// before making a normal, compiler-generated call to the real hsearch().
static _Thread_local long long g_search_compar_value;
static _Thread_local int g_search_faulted;

static int search_compar_trampoline(const void *a, const void *b) {
    VirtualMachine *vm = cccc_current_ffi_vm();
    long long args[2] = { (long long)(intptr_t)a, (long long)(intptr_t)b };
    long long result = 0;
    if (!vm || cccc_call_guest_callback(vm, g_search_compar_value, args, 2, &result) != 0) {
        g_search_faulted = 1;
        return 0;
    }
    return (int)result;
}

static long long wrap_hsearch(long long entry_ptr, long long action) {
    // ENTRY is passed by value from the guest, which CCCC marshals as a
    // pointer to a caller-side scratch copy of the struct (the same
    // struct-by-value FFI convention #714 established for vector args),
    // not decomposed into separate key/data scalar slots -- confirmed
    // empirically, since a 3-scalar-arg registration silently shifted
    // action into the data slot.
    ENTRY *guest_entry = (ENTRY *)(void *)entry_ptr;
    ENTRY item;
    item.key = guest_entry->key;
    item.data = guest_entry->data;
    ENTRY *r = hsearch(item, (ACTION)action);
    return (long long)(intptr_t)r;
}

static long long wrap_lfind(long long key, long long base, long long nmemb,
                            long long size, long long compar) {
    long long saved = g_search_compar_value;
    int saved_faulted = g_search_faulted;
    g_search_compar_value = compar;
    g_search_faulted = 0;
    void *r = lfind((const void *)key, (const void *)base, (size_t *)nmemb,
                     (size_t)size, compar ? search_compar_trampoline : NULL);
    int faulted = g_search_faulted;
    g_search_compar_value = saved;
    g_search_faulted = saved_faulted;
    if (faulted) errno = EFAULT;
    return (long long)(intptr_t)r;
}

static long long wrap_lsearch(long long key, long long base, long long nmemb,
                               long long size, long long compar) {
    long long saved = g_search_compar_value;
    int saved_faulted = g_search_faulted;
    g_search_compar_value = compar;
    g_search_faulted = 0;
    void *r = lsearch((const void *)key, (void *)base, (size_t *)nmemb,
                       (size_t)size, compar ? search_compar_trampoline : NULL);
    int faulted = g_search_faulted;
    g_search_compar_value = saved;
    g_search_faulted = saved_faulted;
    if (faulted) errno = EFAULT;
    return (long long)(intptr_t)r;
}

static long long wrap_tsearch(long long key, long long rootp, long long compar) {
    long long saved = g_search_compar_value;
    int saved_faulted = g_search_faulted;
    g_search_compar_value = compar;
    g_search_faulted = 0;
    void *r = tsearch((const void *)key, (void **)rootp,
                       compar ? search_compar_trampoline : NULL);
    int faulted = g_search_faulted;
    g_search_compar_value = saved;
    g_search_faulted = saved_faulted;
    if (faulted) errno = EFAULT;
    return (long long)(intptr_t)r;
}

static long long wrap_tfind(long long key, long long rootp, long long compar) {
    long long saved = g_search_compar_value;
    int saved_faulted = g_search_faulted;
    g_search_compar_value = compar;
    g_search_faulted = 0;
    void *r = tfind((const void *)key, (void *const *)rootp,
                     compar ? search_compar_trampoline : NULL);
    int faulted = g_search_faulted;
    g_search_compar_value = saved;
    g_search_faulted = saved_faulted;
    if (faulted) errno = EFAULT;
    return (long long)(intptr_t)r;
}

static long long wrap_tdelete(long long key, long long rootp, long long compar) {
    long long saved = g_search_compar_value;
    int saved_faulted = g_search_faulted;
    g_search_compar_value = compar;
    g_search_faulted = 0;
    void *r = tdelete((const void *)key, (void **)rootp,
                       compar ? search_compar_trampoline : NULL);
    int faulted = g_search_faulted;
    g_search_compar_value = saved;
    g_search_faulted = saved_faulted;
    if (faulted) errno = EFAULT;
    return (long long)(intptr_t)r;
}

static _Thread_local long long g_twalk_action_value;
static _Thread_local int g_twalk_faulted;

static void twalk_action_trampoline(const void *nodep, VISIT which, int depth) {
    VirtualMachine *vm = cccc_current_ffi_vm();
    long long args[3] = { (long long)(intptr_t)nodep, (long long)(int)which, (long long)depth };
    long long result = 0;
    if (!vm || cccc_call_guest_callback(vm, g_twalk_action_value, args, 3, &result) != 0)
        g_twalk_faulted = 1;
}

static long long wrap_twalk(long long root, long long action) {
    long long saved = g_twalk_action_value;
    int saved_faulted = g_twalk_faulted;
    g_twalk_action_value = action;
    g_twalk_faulted = 0;
    twalk((const void *)root, action ? twalk_action_trampoline : NULL);
    int faulted = g_twalk_faulted;
    g_twalk_action_value = saved;
    g_twalk_faulted = saved_faulted;
    if (faulted) errno = EFAULT;
    return 0;
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

// sys/statvfs.h (#799) -- struct statvfs diverges even harder than statfs
// (64 bytes/32-bit counters on macOS vs 112 bytes/64-bit counters on
// Linux), so the guest gets a CCCC-canonical struct in POSIX field order
// with wide counters on both platforms, populated field-by-field from a
// host-local struct statvfs -- same shape as copy_statfs_fields above.
struct cccc_guest_statvfs {
    unsigned long f_bsize;
    unsigned long f_frsize;
    unsigned long f_blocks;
    unsigned long f_bfree;
    unsigned long f_bavail;
    unsigned long f_files;
    unsigned long f_ffree;
    unsigned long f_favail;
    unsigned long f_fsid;
    unsigned long f_flag;
    unsigned long f_namemax;
};

static void copy_statvfs_fields(const struct statvfs *host_buf, long long guest_ptr) {
    struct cccc_guest_statvfs *g = (struct cccc_guest_statvfs *)(void *)guest_ptr;
    g->f_bsize   = (unsigned long)host_buf->f_bsize;
    g->f_frsize  = (unsigned long)host_buf->f_frsize;
    g->f_blocks  = (unsigned long)host_buf->f_blocks;
    g->f_bfree   = (unsigned long)host_buf->f_bfree;
    g->f_bavail  = (unsigned long)host_buf->f_bavail;
    g->f_files   = (unsigned long)host_buf->f_files;
    g->f_ffree   = (unsigned long)host_buf->f_ffree;
    g->f_favail  = (unsigned long)host_buf->f_favail;
    g->f_fsid    = (unsigned long)host_buf->f_fsid;
    g->f_flag    = (unsigned long)host_buf->f_flag;
    g->f_namemax = (unsigned long)host_buf->f_namemax;
}

static long long wrap_statvfs(long long path, long long buf) {
    struct statvfs host_buf;
    int rc = statvfs((const char *)path, &host_buf);
    if (rc == 0 && buf) copy_statvfs_fields(&host_buf, buf);
    return rc;
}

static long long wrap_fstatvfs(long long fd, long long buf) {
    struct statvfs host_buf;
    int rc = fstatvfs((int)fd, &host_buf);
    if (rc == 0 && buf) copy_statvfs_fields(&host_buf, buf);
    return rc;
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
    case 0: return SCHED_OTHER; // guest SCHED_OTHER
    case 1: return SCHED_FIFO;  // guest SCHED_FIFO
    case 2: return SCHED_RR;    // guest SCHED_RR
#ifdef __linux__
    case 3: return SCHED_BATCH; // guest SCHED_BATCH
    case 5: return SCHED_IDLE;  // guest SCHED_IDLE
#endif
    default: return guest_policy;
    }
}

struct cccc_guest_sched_param {
    int sched_priority;
};

static long long wrap_sched_get_priority_min(long long policy) {
    return (long long)sched_get_priority_min(guest_to_host_sched_policy((int)policy));
}

static long long wrap_sched_get_priority_max(long long policy) {
    return (long long)sched_get_priority_max(guest_to_host_sched_policy((int)policy));
}

#ifdef __linux__
static long long wrap_sched_setparam(long long pid, long long param) {
    struct sched_param host_param = {0};
    if (param) host_param.sched_priority = ((struct cccc_guest_sched_param *)(void *)param)->sched_priority;
    return (long long)sched_setparam((pid_t)pid, &host_param);
}

static long long wrap_sched_getparam(long long pid, long long param) {
    struct sched_param host_param;
    int rc = sched_getparam((pid_t)pid, &host_param);
    if (rc == 0 && param)
        ((struct cccc_guest_sched_param *)(void *)param)->sched_priority = host_param.sched_priority;
    return rc;
}

static long long wrap_sched_setscheduler(long long pid, long long policy, long long param) {
    struct sched_param host_param = {0};
    if (param) host_param.sched_priority = ((struct cccc_guest_sched_param *)(void *)param)->sched_priority;
    int rc = sched_setscheduler((pid_t)pid, guest_to_host_sched_policy((int)policy), &host_param);
    return rc;
}

static int host_to_guest_sched_policy(int host_policy) {
    if (host_policy == SCHED_OTHER) return 0;
    if (host_policy == SCHED_FIFO)  return 1;
    if (host_policy == SCHED_RR)    return 2;
    if (host_policy == SCHED_BATCH) return 3;
    if (host_policy == SCHED_IDLE)  return 5;
    return host_policy;
}

static long long wrap_sched_getscheduler(long long pid) {
    int rc = sched_getscheduler((pid_t)pid);
    if (rc < 0) return rc;
    return host_to_guest_sched_policy(rc);
}

static long long wrap_sched_rr_get_interval(long long pid, long long interval) {
    return (long long)sched_rr_get_interval((pid_t)pid, (struct timespec *)interval);
}
#else
static long long wrap_sched_setparam(long long pid, long long param) {
    (void)pid; (void)param;
    errno = ENOSYS;
    return -1;
}

static long long wrap_sched_getparam(long long pid, long long param) {
    (void)pid; (void)param;
    errno = ENOSYS;
    return -1;
}

static long long wrap_sched_setscheduler(long long pid, long long policy, long long param) {
    (void)pid; (void)policy; (void)param;
    errno = ENOSYS;
    return -1;
}

static long long wrap_sched_getscheduler(long long pid) {
    (void)pid;
    errno = ENOSYS;
    return -1;
}

static long long wrap_sched_rr_get_interval(long long pid, long long interval) {
    (void)pid; (void)interval;
    errno = ENOSYS;
    return -1;
}
#endif

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
    uid_t uid;
    gid_t gid;
    uid_t cuid;
    gid_t cgid;
    mode_t mode;
};

static void host_to_guest_ipc_perm(const struct ipc_perm *host, struct cccc_guest_ipc_perm *g) {
    g->uid  = host->uid;
    g->gid  = host->gid;
    g->cuid = host->cuid;
    g->cgid = host->cgid;
    g->mode = host->mode;
}

static void guest_overlay_ipc_perm(struct ipc_perm *host, const struct cccc_guest_ipc_perm *g) {
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
    unsigned long shm_segsz;
    pid_t shm_lpid;
    pid_t shm_cpid;
    unsigned long shm_nattch;
    time_t shm_atime;
    time_t shm_dtime;
    time_t shm_ctime;
};

static void host_to_guest_shmid_ds(const struct shmid_ds *host, struct cccc_guest_shmid_ds *g) {
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

static long long wrap_shmat(long long shmid, long long shmaddr, long long shmflg) {
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
        struct shminfo info;
        struct shm_info sinfo;
#endif
    } host;
    memset(&host, 0, sizeof(host));

    if (cmd == IPC_SET) {
        if (shmctl((int)shmid, IPC_STAT, &host.ds) != 0) return -1;
        if (buf) guest_overlay_ipc_perm(&host.ds.shm_perm,
            &((struct cccc_guest_shmid_ds *)(void *)buf)->shm_perm);
    }

    int rc = shmctl((int)shmid, (int)cmd, &host.ds);
    if (rc == 0 && buf) {
        if (cmd == IPC_STAT
#ifdef __linux__
            || cmd == SHM_STAT || cmd == SHM_STAT_ANY
#endif
        ) {
            host_to_guest_shmid_ds(&host.ds, (struct cccc_guest_shmid_ds *)(void *)buf);
        }
#ifdef __linux__
        else if (cmd == IPC_INFO) {
            struct shminfo *g = (struct shminfo *)(void *)buf;
            g->shmmax = host.info.shmmax;
            g->shmmin = host.info.shmmin;
            g->shmmni = host.info.shmmni;
            g->shmseg = host.info.shmseg;
            g->shmall = host.info.shmall;
        } else if (cmd == SHM_INFO) {
            struct shm_info *g = (struct shm_info *)(void *)buf;
            g->used_ids       = host.sinfo.used_ids;
            g->shm_tot        = (unsigned long)host.sinfo.shm_tot;
            g->shm_rss        = (unsigned long)host.sinfo.shm_rss;
            g->shm_swp        = (unsigned long)host.sinfo.shm_swp;
            g->swap_attempts  = (unsigned long)host.sinfo.swap_attempts;
            g->swap_successes = (unsigned long)host.sinfo.swap_successes;
        }
#endif
    }
    return rc;
}

// --- sys/sem.h -----------------------------------------------------------

struct cccc_guest_semid_ds {
    struct cccc_guest_ipc_perm sem_perm;
    unsigned short sem_nsems;
    time_t sem_otime;
    time_t sem_ctime;
};

static void host_to_guest_semid_ds(const struct semid_ds *host, struct cccc_guest_semid_ds *g) {
    host_to_guest_ipc_perm(&host->sem_perm, &g->sem_perm);
    g->sem_nsems = (unsigned short)host->sem_nsems;
    g->sem_otime = host->sem_otime;
    g->sem_ctime = host->sem_ctime;
}

static long long wrap_semget(long long key, long long nsems, long long semflg) {
    return (long long)semget((key_t)key, (int)nsems, (int)semflg);
}

static long long wrap_semop(long long semid, long long sops, long long nsops) {
    VirtualMachine *vm = current_vm();
    if (!vm || !vm->gil_initialized)
        return (long long)semop((int)semid, (struct sembuf *)(void *)sops, (size_t)nsops);
    ExecState state;
    posix_save_and_release_gil(vm, &state);
    int r = semop((int)semid, (struct sembuf *)(void *)sops, (size_t)nsops);
    posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

static long long wrap_semctl(long long semid, long long semnum, long long cmd, ...) {
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
        return (long long)semctl((int)semid, (int)semnum, (int)cmd, (int)arg);
    case GETALL:
    case SETALL:
        return (long long)semctl((int)semid, (int)semnum, (int)cmd,
                                 (unsigned short *)(intptr_t)arg);
    case IPC_STAT:
    case IPC_SET: {
        struct semid_ds host_buf;
        memset(&host_buf, 0, sizeof(host_buf));
        if (cmd == IPC_SET) {
            if (semctl((int)semid, (int)semnum, IPC_STAT, &host_buf) != 0) return -1;
            if (arg) guest_overlay_ipc_perm(&host_buf.sem_perm,
                &((struct cccc_guest_semid_ds *)(void *)(intptr_t)arg)->sem_perm);
        }
        int rc = semctl((int)semid, (int)semnum, (int)cmd, &host_buf);
        if (rc == 0 && cmd == IPC_STAT && arg)
            host_to_guest_semid_ds(&host_buf, (struct cccc_guest_semid_ds *)(void *)(intptr_t)arg);
        return rc;
    }
#ifdef __linux__
    case SEM_STAT:
    case SEM_STAT_ANY: {
        struct semid_ds host_buf;
        memset(&host_buf, 0, sizeof(host_buf));
        int rc = semctl((int)semid, (int)semnum, (int)cmd, &host_buf);
        if (rc >= 0 && arg)
            host_to_guest_semid_ds(&host_buf, (struct cccc_guest_semid_ds *)(void *)(intptr_t)arg);
        return rc;
    }
    case SEM_INFO: {
        struct seminfo host_info;
        memset(&host_info, 0, sizeof(host_info));
        int rc = semctl((int)semid, (int)semnum, (int)cmd, &host_info);
        if (rc >= 0 && arg) {
            struct seminfo *g = (struct seminfo *)(void *)(intptr_t)arg;
            *g = host_info;
        }
        return rc;
    }
#endif
    default:
        return (long long)semctl((int)semid, (int)semnum, (int)cmd, (int)arg);
    }
}

// --- sys/msg.h -------------------------------------------------------------

struct cccc_guest_msqid_ds {
    struct cccc_guest_ipc_perm msg_perm;
    unsigned long msg_qnum;
    unsigned long msg_qbytes;
    pid_t msg_lspid;
    pid_t msg_lrpid;
    time_t msg_stime;
    time_t msg_rtime;
    time_t msg_ctime;
};

static void host_to_guest_msqid_ds(const struct msqid_ds *host, struct cccc_guest_msqid_ds *g) {
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

static long long wrap_msgsnd(long long msqid, long long msgp, long long msgsz, long long msgflg) {
    VirtualMachine *vm = current_vm();
    if (!vm || !vm->gil_initialized)
        return (long long)msgsnd((int)msqid, (const void *)msgp, (size_t)msgsz, (int)msgflg);
    ExecState state;
    posix_save_and_release_gil(vm, &state);
    int r = msgsnd((int)msqid, (const void *)msgp, (size_t)msgsz, (int)msgflg);
    posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

static long long wrap_msgrcv(long long msqid, long long msgp, long long msgsz, long long msgtyp, long long msgflg) {
    VirtualMachine *vm = current_vm();
    if (!vm || !vm->gil_initialized)
        return (long long)msgrcv((int)msqid, (void *)msgp, (size_t)msgsz, (long)msgtyp, (int)msgflg);
    ExecState state;
    posix_save_and_release_gil(vm, &state);
    ssize_t r = msgrcv((int)msqid, (void *)msgp, (size_t)msgsz, (long)msgtyp, (int)msgflg);
    posix_acquire_and_restore_gil(vm, &state);
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
        if (msgctl((int)msqid, IPC_STAT, &host.ds) != 0) return -1;
        if (buf) guest_overlay_ipc_perm(&host.ds.msg_perm,
            &((struct cccc_guest_msqid_ds *)(void *)buf)->msg_perm);
    }

    int rc = msgctl((int)msqid, (int)cmd, &host.ds);
    if (rc == 0 && buf) {
        if (cmd == IPC_STAT
#ifdef __linux__
            || cmd == MSG_STAT || cmd == MSG_STAT_ANY
#endif
        ) {
            host_to_guest_msqid_ds(&host.ds, (struct cccc_guest_msqid_ds *)(void *)buf);
        }
#ifdef __linux__
        else if (cmd == IPC_INFO || cmd == MSG_INFO) {
            struct msginfo *g = (struct msginfo *)(void *)buf;
            *g = host.info;
        }
#endif
    }
    return rc;
}

// wordexp.h (#802) -- wordexp_t is byte-identical on macOS and glibc
// ({ size_t we_wordc; char **we_wordv; size_t we_offs; }, verified against
// real headers), so it's a plain pass-through; only the WRDE_* constants
// diverge (include/wordexp.h splits those, same pattern as glob.h's
// GLOB_*). wordexp() forks a shell, so it releases the GIL like the other
// blocking wrappers.
static long long wrap_wordexp(long long words, long long we, long long flags) {
    VirtualMachine *vm = current_vm();
    if (!vm || !vm->gil_initialized)
        return (long long)wordexp((const char *)words, (wordexp_t *)(void *)we, (int)flags);
    ExecState state;
    posix_save_and_release_gil(vm, &state);
    int r = wordexp((const char *)words, (wordexp_t *)(void *)we, (int)flags);
    posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

static long long wrap_wordfree(long long we) {
    wordfree((wordexp_t *)(void *)we);
    return 0;
}

// SIGEV_THREAD (#870) --------------------------------------------------
// A host notification thread (spawned by the real aio_*()/mq_notify()
// implementation) invokes cccc_sigev_thread_trampoline() with no VM state
// available on that thread (cccc_current_ffi_vm()'s _Thread_local is only
// ever set on the FFI-call path, ops.c) -- so the trampoline must be
// async-signal-safe-equivalent: it only latches a pending cookie, mirroring
// _cccc_sig_shim() in src/stdlib/signal.c. The VM's own dispatch loop
// (src/vm.c, right after its pending-signal poll) picks the cookie up at
// its existing safe point and runs the guest sigev_notify_function there.
// This is deferred delivery on the VM thread, not concurrent execution on
// the notification thread -- a guest blocked inside a host call only
// observes the notification once that call returns and the dispatch loop
// is reached again. CCCC_SIGEV_MAX is defined in internal.h, next to the
// pending-flag arrays it sizes.

typedef struct {
    int in_use;
    long long guest_fn;    // guest sigev_notify_function, as a raw byte
                            // offset (same encoding wrap_sigaction's
                            // handler_fn uses; converted at delivery time
                            // via cc_byte_offset_to_pc())
    long long guest_sival; // original guest union sigval, passed through
} SigevCookie;

static SigevCookie g_sigev_cookies[CCCC_SIGEV_MAX];
static pthread_mutex_t g_sigev_mutex = PTHREAD_MUTEX_INITIALIZER;

// Mirrors _cccc_pending/_cccc_any_pending in src/stdlib/signal.c; polled by
// the same dispatch-loop safe point (src/vm.c).
volatile sig_atomic_t _cccc_sigev_pending[CCCC_SIGEV_MAX];
volatile sig_atomic_t _cccc_sigev_any_pending;

static int sigev_cookie_alloc(long long guest_fn, long long guest_sival) {
    pthread_mutex_lock(&g_sigev_mutex);
    int idx = -1;
    for (int i = 0; i < CCCC_SIGEV_MAX; i++) {
        if (!g_sigev_cookies[i].in_use) {
            g_sigev_cookies[i].in_use = 1;
            g_sigev_cookies[i].guest_fn = guest_fn;
            g_sigev_cookies[i].guest_sival = guest_sival;
            idx = i;
            break;
        }
    }
    pthread_mutex_unlock(&g_sigev_mutex);
    return idx;
}

void cccc_sigev_cookie_free(int idx) {
    if (idx < 0 || idx >= CCCC_SIGEV_MAX)
        return;
    pthread_mutex_lock(&g_sigev_mutex);
    g_sigev_cookies[idx].in_use = 0;
    _cccc_sigev_pending[idx] = 0;
    pthread_mutex_unlock(&g_sigev_mutex);
}

// Guest-to-guest calls pass a struct/union-typed argument by reference (the
// callee's parameter aliases the caller's own storage, src/codegen.c's
// ND_FUNCALL argument loop -- there is no dedicated by-value struct/union
// case, unlike vectors), so REG_A0 for `void notify(union sigval sv)` must
// be an address, not the raw 8 bytes. g_sigev_cookies[idx].guest_sival is a
// static host array slot -- outside the VM's own stack segment entirely,
// same trick cccc_guest_siginfo_for() (src/stdlib/signal.c) uses for
// siginfo_t -- so it needs no STKTAG/epoch bookkeeping and CHKP3's
// stack-range check simply doesn't apply to it. The slot's bytes stay
// valid after cccc_sigev_cookie_free() (which only clears in_use, not the
// bytes), covering the delivery window between the dispatch loop reading
// this address and the guest handler dereferencing it; only a fresh
// registration reusing the same idx before that dereference would corrupt
// it, a narrow race bounded by CCCC_SIGEV_MAX concurrent registrations.
int cccc_sigev_cookie_guest_fn(int idx, long long *out_fn, long long *out_sival_addr) {
    if (idx < 0 || idx >= CCCC_SIGEV_MAX || !g_sigev_cookies[idx].in_use)
        return 0;
    *out_fn = g_sigev_cookies[idx].guest_fn;
    *out_sival_addr = (long long)(intptr_t)&g_sigev_cookies[idx].guest_sival;
    return 1;
}

static void cccc_sigev_thread_trampoline(union sigval sv) {
    int idx = sv.sival_int;
    if (idx < 0 || idx >= CCCC_SIGEV_MAX || !g_sigev_cookies[idx].in_use)
        return;
    _cccc_sigev_pending[idx] = 1;
    _cccc_sigev_any_pending = 1;
}

// aio.h (#804) -- struct aiocb is declared byte-exact per platform in
// include/aio.h (see its comment); every aio_*()/lio_listio() argument is
// a pointer, so these wrappers are plain pointer-cast pass-throughs, no
// marshalling needed. aio_suspend()/lio_listio(LIO_WAIT) can block for an
// unbounded time waiting on completion, so they release the GIL;
// aio_read()/aio_write()/aio_error()/aio_return()/aio_cancel()/aio_fsync()
// only enqueue or poll state and return promptly, so they keep it,
// matching opendir()'s category above. SIGEV_THREAD (see signal.h's
// struct sigevent comment) is honored via sigevent_prepare() above, which
// swaps in cccc_sigev_thread_trampoline() as the real host notification
// function; SIGEV_NONE/SIGEV_SIGNAL pass through unchanged.
static int sigevent_prepare(struct sigevent *sev) {
    if (!sev)
        return 1;
    if (sev->sigev_notify != SIGEV_THREAD)
        return 1;
    if (!sev->sigev_notify_function) {
        errno = EINVAL;
        return 0;
    }
    long long guest_fn = (long long)(intptr_t)sev->sigev_notify_function;
    long long guest_sival;
    memcpy(&guest_sival, &sev->sigev_value, sizeof(guest_sival));
    int idx = sigev_cookie_alloc(guest_fn, guest_sival);
    if (idx < 0) {
        errno = EAGAIN;
        return 0;
    }
    sev->sigev_notify_function = cccc_sigev_thread_trampoline;
    sev->sigev_notify_attributes = NULL;
    sev->sigev_value.sival_int = idx;
    return 1;
}

static long long wrap_aio_read(long long aiocbp) {
    struct aiocb *cb = (struct aiocb *)(intptr_t)aiocbp;
    if (!cb) {
        errno = EINVAL;
        return -1;
    }
    if (!sigevent_prepare(&cb->aio_sigevent))
        return -1;
    return (long long)aio_read(cb);
}

static long long wrap_aio_write(long long aiocbp) {
    struct aiocb *cb = (struct aiocb *)(intptr_t)aiocbp;
    if (!cb) {
        errno = EINVAL;
        return -1;
    }
    if (!sigevent_prepare(&cb->aio_sigevent))
        return -1;
    return (long long)aio_write(cb);
}

static long long wrap_aio_error(long long aiocbp) {
    return (long long)aio_error((const struct aiocb *)(intptr_t)aiocbp);
}

static long long wrap_aio_return(long long aiocbp) {
    return (long long)aio_return((struct aiocb *)(intptr_t)aiocbp);
}

static long long wrap_aio_cancel(long long fd, long long aiocbp) {
    struct aiocb *cb = (struct aiocb *)(intptr_t)aiocbp;
    // A single canceled request's SIGEV_THREAD cookie (its index is stashed
    // in aio_sigevent.sigev_value by sigevent_prepare()) is freed here since
    // no notification will ever arrive for it. aiocbp == NULL cancels every
    // outstanding request on fd at once; those cookies are not individually
    // trackable from here and are freed lazily -- see cccc_sigev_cookie_free
    // callers -- rather than leaked forever (bounded by CCCC_SIGEV_MAX).
    if (cb && cb->aio_sigevent.sigev_notify == SIGEV_THREAD)
        cccc_sigev_cookie_free((int)cb->aio_sigevent.sigev_value.sival_int);
    return (long long)aio_cancel((int)fd, cb);
}

static long long wrap_aio_fsync(long long op, long long aiocbp) {
    struct aiocb *cb = (struct aiocb *)(intptr_t)aiocbp;
    if (!cb) {
        errno = EINVAL;
        return -1;
    }
    if (!sigevent_prepare(&cb->aio_sigevent))
        return -1;
    return (long long)aio_fsync((int)op, cb);
}

static long long wrap_aio_suspend(long long aiocblist, long long nent, long long timeoutp) {
    VirtualMachine *vm = current_vm();
    const struct aiocb *const *list = (const struct aiocb *const *)(intptr_t)aiocblist;
    const struct timespec *tp = (const struct timespec *)(intptr_t)timeoutp;
    if (!vm || !vm->gil_initialized)
        return (long long)aio_suspend(list, (int)nent, tp);
    ExecState state;
    posix_save_and_release_gil(vm, &state);
    int r = aio_suspend(list, (int)nent, tp);
    posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

static long long wrap_lio_listio(long long mode, long long aiocblist, long long nent, long long sigp) {
    struct sigevent *sev = (struct sigevent *)(intptr_t)sigp;
    if (!sigevent_prepare(sev))
        return -1;
    struct aiocb *const *list = (struct aiocb *const *)(intptr_t)aiocblist;
    // Each list entry carries its own per-request aio_sigevent (LIO_NOP
    // entries are skipped by aio_lio_opcode; the pointer itself must still
    // be non-NULL to check, matching the real lio_listio() contract).
    for (long long i = 0; i < nent; i++) {
        if (!list[i])
            continue;
        if (!sigevent_prepare(&list[i]->aio_sigevent))
            return -1;
    }
    VirtualMachine *vm = current_vm();
    if (!vm || !vm->gil_initialized || (int)mode != LIO_WAIT)
        return (long long)lio_listio((int)mode, list, (int)nent, sev);
    ExecState state;
    posix_save_and_release_gil(vm, &state);
    int r = lio_listio((int)mode, list, (int)nent, sev);
    posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

#ifdef __linux__
// mqueue.h (#805) -- Linux-only (see include/mqueue.h). mq_send/
// mq_receive/mq_timedsend/mq_timedreceive block on a full/empty queue, so
// they release the GIL; mq_open/mq_close/mq_unlink/mq_notify/mq_setattr/
// mq_getattr return promptly and keep it. mq_notify()'s SIGEV_THREAD is
// honored the same way aio's is, via sigevent_prepare() above (struct
// sigevent has the same layout regardless of which header pulled it in).
static long long wrap_mq_open(const char *name, long long oflag, ...) {
    mqd_t r;
    if ((int)oflag & O_CREAT) {
        va_list ap;
        va_start(ap, oflag);
        mode_t mode = (mode_t)(unsigned int)va_arg(ap, unsigned int);
        struct mq_attr *attr = va_arg(ap, struct mq_attr *);
        va_end(ap);
        r = mq_open(name, (int)oflag, mode, attr);
    } else {
        r = mq_open(name, (int)oflag);
    }
    return (long long)r;
}

static long long wrap_mq_close(long long mqdes) {
    return (long long)mq_close((mqd_t)mqdes);
}

static long long wrap_mq_unlink(long long name) {
    return (long long)mq_unlink((const char *)(intptr_t)name);
}

static long long wrap_mq_send(long long mqdes, long long msg_ptr, long long msg_len, long long msg_prio) {
    VirtualMachine *vm = current_vm();
    const char *p = (const char *)(intptr_t)msg_ptr;
    if (!vm || !vm->gil_initialized)
        return (long long)mq_send((mqd_t)mqdes, p, (size_t)msg_len, (unsigned int)msg_prio);
    ExecState state;
    posix_save_and_release_gil(vm, &state);
    int r = mq_send((mqd_t)mqdes, p, (size_t)msg_len, (unsigned int)msg_prio);
    posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

static long long wrap_mq_receive(long long mqdes, long long msg_ptr, long long msg_len, long long msg_prio) {
    VirtualMachine *vm = current_vm();
    char *p = (char *)(intptr_t)msg_ptr;
    unsigned int *prio = (unsigned int *)(intptr_t)msg_prio;
    if (!vm || !vm->gil_initialized)
        return (long long)mq_receive((mqd_t)mqdes, p, (size_t)msg_len, prio);
    ExecState state;
    posix_save_and_release_gil(vm, &state);
    ssize_t r = mq_receive((mqd_t)mqdes, p, (size_t)msg_len, prio);
    posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

static long long wrap_mq_timedsend(long long mqdes, long long msg_ptr, long long msg_len,
                                   long long msg_prio, long long abs_timeout) {
    VirtualMachine *vm = current_vm();
    const char *p = (const char *)(intptr_t)msg_ptr;
    const struct timespec *ts = (const struct timespec *)(intptr_t)abs_timeout;
    if (!vm || !vm->gil_initialized)
        return (long long)mq_timedsend((mqd_t)mqdes, p, (size_t)msg_len, (unsigned int)msg_prio, ts);
    ExecState state;
    posix_save_and_release_gil(vm, &state);
    int r = mq_timedsend((mqd_t)mqdes, p, (size_t)msg_len, (unsigned int)msg_prio, ts);
    posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

static long long wrap_mq_timedreceive(long long mqdes, long long msg_ptr, long long msg_len,
                                      long long msg_prio, long long abs_timeout) {
    VirtualMachine *vm = current_vm();
    char *p = (char *)(intptr_t)msg_ptr;
    unsigned int *prio = (unsigned int *)(intptr_t)msg_prio;
    const struct timespec *ts = (const struct timespec *)(intptr_t)abs_timeout;
    if (!vm || !vm->gil_initialized)
        return (long long)mq_timedreceive((mqd_t)mqdes, p, (size_t)msg_len, prio, ts);
    ExecState state;
    posix_save_and_release_gil(vm, &state);
    ssize_t r = mq_timedreceive((mqd_t)mqdes, p, (size_t)msg_len, prio, ts);
    posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

// mq_notify() registers at most one outstanding notification per queue and
// NULL deregisters it (POSIX); track which SIGEV_THREAD cookie (if any) is
// bound to a given mqdes so deregistration -- or replacement by a later
// mq_notify() call -- frees it instead of leaking a slot in
// g_sigev_cookies.
typedef struct {
    int in_use;
    mqd_t mqdes;
    int cookie_idx;
} MqNotifyBinding;
static MqNotifyBinding g_mq_notify_bindings[CCCC_SIGEV_MAX];

static void mq_notify_binding_clear(mqd_t mqdes) {
    for (int i = 0; i < CCCC_SIGEV_MAX; i++) {
        if (g_mq_notify_bindings[i].in_use && g_mq_notify_bindings[i].mqdes == mqdes) {
            cccc_sigev_cookie_free(g_mq_notify_bindings[i].cookie_idx);
            g_mq_notify_bindings[i].in_use = 0;
        }
    }
}

static void mq_notify_binding_set(mqd_t mqdes, int cookie_idx) {
    mq_notify_binding_clear(mqdes);
    for (int i = 0; i < CCCC_SIGEV_MAX; i++) {
        if (!g_mq_notify_bindings[i].in_use) {
            g_mq_notify_bindings[i].in_use = 1;
            g_mq_notify_bindings[i].mqdes = mqdes;
            g_mq_notify_bindings[i].cookie_idx = cookie_idx;
            return;
        }
    }
}

static long long wrap_mq_notify(long long mqdes, long long notification) {
    struct sigevent *sev = (struct sigevent *)(intptr_t)notification;
    if (!sev) {
        mq_notify_binding_clear((mqd_t)mqdes);
        return (long long)mq_notify((mqd_t)mqdes, NULL);
    }
    int is_thread = (sev->sigev_notify == SIGEV_THREAD);
    if (!sigevent_prepare(sev))
        return -1;
    int cookie_idx = is_thread ? sev->sigev_value.sival_int : -1;
    int r = mq_notify((mqd_t)mqdes, sev);
    if (r != 0) {
        if (cookie_idx >= 0)
            cccc_sigev_cookie_free(cookie_idx);
        return (long long)r;
    }
    if (cookie_idx >= 0)
        mq_notify_binding_set((mqd_t)mqdes, cookie_idx);
    else
        mq_notify_binding_clear((mqd_t)mqdes); // SIGEV_NONE/SIGEV_SIGNAL replacing a prior THREAD registration
    return 0;
}

static long long wrap_mq_setattr(long long mqdes, long long newattr, long long oldattr) {
    return (long long)mq_setattr((mqd_t)mqdes, (const struct mq_attr *)(intptr_t)newattr,
                                 (struct mq_attr *)(intptr_t)oldattr);
}

static long long wrap_mq_getattr(long long mqdes, long long attr) {
    return (long long)mq_getattr((mqd_t)mqdes, (struct mq_attr *)(intptr_t)attr);
}
#endif

#if defined(__APPLE__) || defined(CCCC_HAS_NDBM)
// ndbm.h (#810, #871) -- macOS/BSD natively; on Linux only when built with
// CCCC_HAS_NDBM=1 against libgdbm-compat (see include/ndbm.h). Guarded
// independently of the __linux__ split above (not "not Linux"), since a
// Linux build can go either way depending on the knob.
//
// dbm_fetch/dbm_firstkey/dbm_nextkey/dbm_delete/dbm_store take or return
// `datum` by value; CCCC's FFI marshalling doesn't correctly handle a
// struct/union crossing the host-call boundary by value (only a single
// 64-bit slot is marshalled per argument/return -- same gap as
// wrap_semctl's union semun above), so these five __cccc_dbm_* helpers
// are scalar-only (pointer + length in, pointer + length out) and
// include/ndbm.h's `static inline` shims reassemble the real
// `datum dbm_fetch(DBM*, datum)` shape entirely on the guest side, never
// crossing the FFI boundary with an aggregate.
static long long wrap_dbm_fetch(long long db, long long kptr, long long klen,
                                long long out_dptr, long long out_dsize) {
    datum key = { (void *)(intptr_t)kptr, (size_t)klen };
    datum r = dbm_fetch((DBM *)(intptr_t)db, key);
    *(void **)(intptr_t)out_dptr = r.dptr;
    *(size_t *)(intptr_t)out_dsize = r.dsize;
    return 0;
}

static long long wrap_dbm_firstkey(long long db, long long out_dptr, long long out_dsize) {
    datum r = dbm_firstkey((DBM *)(intptr_t)db);
    *(void **)(intptr_t)out_dptr = r.dptr;
    *(size_t *)(intptr_t)out_dsize = r.dsize;
    return 0;
}

static long long wrap_dbm_nextkey(long long db, long long out_dptr, long long out_dsize) {
    datum r = dbm_nextkey((DBM *)(intptr_t)db);
    *(void **)(intptr_t)out_dptr = r.dptr;
    *(size_t *)(intptr_t)out_dsize = r.dsize;
    return 0;
}

static long long wrap_dbm_delete(long long db, long long kptr, long long klen) {
    datum key = { (void *)(intptr_t)kptr, (size_t)klen };
    return (long long)dbm_delete((DBM *)(intptr_t)db, key);
}

static long long wrap_dbm_store(long long db, long long kptr, long long klen,
                                long long vptr, long long vlen, long long flags) {
    datum key = { (void *)(intptr_t)kptr, (size_t)klen };
    datum content = { (void *)(intptr_t)vptr, (size_t)vlen };
    return (long long)dbm_store((DBM *)(intptr_t)db, key, content, (int)flags);
}

static long long wrap_dbm_open(long long file, long long open_flags, long long file_mode) {
    // gdbm's dbm_open takes a non-const char* (verified in the
    // cccc-linux-amd64 container); macOS's is const char*. Guest code
    // always sees the POSIX `const char *` signature (include/ndbm.h), so
    // the const is cast away here rather than in the guest header.
    return (long long)(intptr_t)dbm_open((char *)(intptr_t)file, (int)open_flags, (mode_t)file_mode);
}

static long long wrap_dbm_close(long long db) {
    dbm_close((DBM *)(intptr_t)db);
    return 0;
}

#ifdef __APPLE__
static long long wrap_dbm_clearerr(long long db) {
    return (long long)dbm_clearerr((DBM *)(intptr_t)db);
}
#else
// gdbm's dbm_clearerr() returns void (not POSIX's int) -- verified in the
// cccc-linux-amd64 container. Always report success to the guest, which
// only ever sees the POSIX `int dbm_clearerr(DBM *)` signature.
static long long wrap_dbm_clearerr(long long db) {
    dbm_clearerr((DBM *)(intptr_t)db);
    return 0;
}
#endif
#endif

// spawn.h (#801) -- posix_spawnattr_t/posix_spawn_file_actions_t are
// opaque pointer-width handles on the guest (see include/spawn.h). *_init
// mallocs a host-sized object (sizeof(posix_spawnattr_t) on the host --
// 336 bytes on glibc, 8 on macOS where the type itself is already a void*
// the kernel manages internally), calls the real host *_init on it, and
// stores that host pointer through the guest's pointer-sized handle;
// *_destroy calls the host destroy then frees it. Every other wrapper
// dereferences the guest handle once to recover the host object pointer
// before calling the corresponding host function.
static long long wrap_posix_spawnattr_init(long long attr_ptr) {
    posix_spawnattr_t *host_attr = malloc(sizeof(posix_spawnattr_t));
    int rc = posix_spawnattr_init(host_attr);
    *(void **)(void *)attr_ptr = (void *)host_attr;
    return rc;
}

static long long wrap_posix_spawnattr_destroy(long long attr_ptr) {
    void *host_attr = *(void **)(void *)attr_ptr;
    int rc = posix_spawnattr_destroy((posix_spawnattr_t *)host_attr);
    free(host_attr);
    return rc;
}

static long long wrap_posix_spawnattr_getflags(long long attr_ptr, long long flags_ptr) {
    void *host_attr = *(void **)(void *)attr_ptr;
    short f = 0;
    int rc = posix_spawnattr_getflags((posix_spawnattr_t *)host_attr, &f);
    if (rc == 0 && flags_ptr) *(short *)(void *)flags_ptr = f;
    return rc;
}

static long long wrap_posix_spawnattr_setflags(long long attr_ptr, long long flags) {
    void *host_attr = *(void **)(void *)attr_ptr;
    return posix_spawnattr_setflags((posix_spawnattr_t *)host_attr, (short)flags);
}

static long long wrap_posix_spawnattr_getpgroup(long long attr_ptr, long long pgroup_ptr) {
    void *host_attr = *(void **)(void *)attr_ptr;
    pid_t p = 0;
    int rc = posix_spawnattr_getpgroup((posix_spawnattr_t *)host_attr, &p);
    if (rc == 0 && pgroup_ptr) *(pid_t *)(void *)pgroup_ptr = p;
    return rc;
}

static long long wrap_posix_spawnattr_setpgroup(long long attr_ptr, long long pgroup) {
    void *host_attr = *(void **)(void *)attr_ptr;
    return posix_spawnattr_setpgroup((posix_spawnattr_t *)host_attr, (pid_t)pgroup);
}

// setsigdefault/setsigmask/getsigdefault/getsigmask translate CCCC's own
// 4-byte guest sigset_t to/from a real host sigset_t, the same conversion
// pselect() uses (guest_sigset_to_host, defined above) plus its reverse.
static unsigned int host_sigset_to_guest(const sigset_t *host_set) {
    unsigned int mask = 0;
    for (int signo = 1; signo < CCCC_NSIG; signo++) {
        if (sigismember(host_set, signo))
            mask |= (1u << (unsigned)(signo - 1));
    }
    return mask;
}

static long long wrap_posix_spawnattr_getsigdefault(long long attr_ptr, long long guest_sigset_ptr) {
    void *host_attr = *(void **)(void *)attr_ptr;
    sigset_t host_set;
    int rc = posix_spawnattr_getsigdefault((posix_spawnattr_t *)host_attr, &host_set);
    if (rc == 0 && guest_sigset_ptr)
        *(unsigned int *)(void *)guest_sigset_ptr = host_sigset_to_guest(&host_set);
    return rc;
}

static long long wrap_posix_spawnattr_setsigdefault(long long attr_ptr, long long guest_sigset_ptr) {
    void *host_attr = *(void **)(void *)attr_ptr;
    sigset_t host_set;
    guest_sigset_to_host(*(unsigned int *)(void *)guest_sigset_ptr, &host_set);
    return posix_spawnattr_setsigdefault((posix_spawnattr_t *)host_attr, &host_set);
}

static long long wrap_posix_spawnattr_getsigmask(long long attr_ptr, long long guest_sigset_ptr) {
    void *host_attr = *(void **)(void *)attr_ptr;
    sigset_t host_set;
    int rc = posix_spawnattr_getsigmask((posix_spawnattr_t *)host_attr, &host_set);
    if (rc == 0 && guest_sigset_ptr)
        *(unsigned int *)(void *)guest_sigset_ptr = host_sigset_to_guest(&host_set);
    return rc;
}

static long long wrap_posix_spawnattr_setsigmask(long long attr_ptr, long long guest_sigset_ptr) {
    void *host_attr = *(void **)(void *)attr_ptr;
    sigset_t host_set;
    guest_sigset_to_host(*(unsigned int *)(void *)guest_sigset_ptr, &host_set);
    return posix_spawnattr_setsigmask((posix_spawnattr_t *)host_attr, &host_set);
}

static long long wrap_posix_spawn_file_actions_init(long long fa_ptr) {
    posix_spawn_file_actions_t *host_fa = malloc(sizeof(posix_spawn_file_actions_t));
    int rc = posix_spawn_file_actions_init(host_fa);
    *(void **)(void *)fa_ptr = (void *)host_fa;
    return rc;
}

static long long wrap_posix_spawn_file_actions_destroy(long long fa_ptr) {
    void *host_fa = *(void **)(void *)fa_ptr;
    int rc = posix_spawn_file_actions_destroy((posix_spawn_file_actions_t *)host_fa);
    free(host_fa);
    return rc;
}

static long long wrap_posix_spawn_file_actions_addopen(long long fa_ptr, long long fildes,
                                                        long long path, long long oflag,
                                                        long long mode) {
    void *host_fa = *(void **)(void *)fa_ptr;
    return posix_spawn_file_actions_addopen((posix_spawn_file_actions_t *)host_fa, (int)fildes,
                                             (const char *)path, (int)oflag, (mode_t)mode);
}

static long long wrap_posix_spawn_file_actions_addclose(long long fa_ptr, long long fildes) {
    void *host_fa = *(void **)(void *)fa_ptr;
    return posix_spawn_file_actions_addclose((posix_spawn_file_actions_t *)host_fa, (int)fildes);
}

static long long wrap_posix_spawn_file_actions_adddup2(long long fa_ptr, long long fildes,
                                                        long long newfildes) {
    void *host_fa = *(void **)(void *)fa_ptr;
    return posix_spawn_file_actions_adddup2((posix_spawn_file_actions_t *)host_fa, (int)fildes,
                                             (int)newfildes);
}

// posix_spawn()/posix_spawnp() fork+exec, so they release the GIL like the
// other blocking wrappers above. argv/envp are guest arrays of guest
// char *, which are already host addresses in CCCC's flat address space --
// same passthrough execv/execve/execvp already rely on, no marshaling
// needed.
static long long wrap_posix_spawn_gil(long long pid_ptr, long long path, long long file_actions_ptr,
                                      long long attrp_ptr, long long argv, long long envp) {
    void *host_fa = file_actions_ptr ? *(void **)(void *)file_actions_ptr : NULL;
    void *host_attr = attrp_ptr ? *(void **)(void *)attrp_ptr : NULL;
    VirtualMachine *vm = current_vm();
    if (!vm || !vm->gil_initialized)
        return (long long)posix_spawn((pid_t *)pid_ptr, (const char *)path,
                                       (const posix_spawn_file_actions_t *)host_fa,
                                       (const posix_spawnattr_t *)host_attr,
                                       (char *const *)argv, (char *const *)envp);
    ExecState state;
    posix_save_and_release_gil(vm, &state);
    int r = posix_spawn((pid_t *)pid_ptr, (const char *)path,
                         (const posix_spawn_file_actions_t *)host_fa,
                         (const posix_spawnattr_t *)host_attr,
                         (char *const *)argv, (char *const *)envp);
    posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

static long long wrap_posix_spawnp_gil(long long pid_ptr, long long file, long long file_actions_ptr,
                                       long long attrp_ptr, long long argv, long long envp) {
    void *host_fa = file_actions_ptr ? *(void **)(void *)file_actions_ptr : NULL;
    void *host_attr = attrp_ptr ? *(void **)(void *)attrp_ptr : NULL;
    VirtualMachine *vm = current_vm();
    if (!vm || !vm->gil_initialized)
        return (long long)posix_spawnp((pid_t *)pid_ptr, (const char *)file,
                                        (const posix_spawn_file_actions_t *)host_fa,
                                        (const posix_spawnattr_t *)host_attr,
                                        (char *const *)argv, (char *const *)envp);
    ExecState state;
    posix_save_and_release_gil(vm, &state);
    int r = posix_spawnp((pid_t *)pid_ptr, (const char *)file,
                          (const posix_spawn_file_actions_t *)host_fa,
                          (const posix_spawnattr_t *)host_attr,
                          (char *const *)argv, (char *const *)envp);
    posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

// nl_langinfo() (#807) -- nl_item values diverge wildly between hosts:
// macOS uses a flat 0-56 sequence (which CCCC's canonical numbering,
// include/langinfo.h, copies verbatim), glibc packs
// (category << 16) | index. The DAY_/ABDAY_/MON_/ABMON_ families are each
// a contiguous run under both numbering schemes, so they translate via
// range arithmetic rather than 46 individual case labels; everything
// else goes through a small table. Anything unrecognized returns ""
// rather than forwarding a bogus nl_item to the host.
//
// The case/range bounds below are written as bare integer literals
// (CCCC's own canonical numbering, matching include/langinfo.h) rather
// than the CODESET/DAY_1/etc. macro names -- this file includes the
// *host's* real <langinfo.h> (there is no -Iinclude in this TU's build,
// see the Makefile), so on Linux those names already expand to glibc's
// real values (e.g. CODESET is 14, not 0) and would silently compare the
// guest's canonical input against the wrong number.
// Shared by wrap_nl_langinfo and wrap_nl_langinfo_l (#820) so the
// translation table/range arithmetic lives in exactly one place.
// *found is set to 0 for an unrecognized canonical item (caller returns ""
// without touching the host at all), 1 otherwise.
static nl_item guest_to_host_nl_item(nl_item guest_item, int *found) {
    *found = 1;
#ifdef __APPLE__
    return guest_item;
#else
    long v = (long)guest_item;
    long host_item;
    switch (v) {
    case 0:  host_item = 14;     break; // CODESET
    case 1:  host_item = 131112; break; // D_T_FMT
    case 2:  host_item = 131113; break; // D_FMT
    case 3:  host_item = 131114; break; // T_FMT
    case 4:  host_item = 131115; break; // T_FMT_AMPM
    case 5:  host_item = 131110; break; // AM_STR
    case 6:  host_item = 131111; break; // PM_STR
    case 50: host_item = 65536;  break; // RADIXCHAR
    case 51: host_item = 65537;  break; // THOUSEP
    case 52: host_item = 327680; break; // YESEXPR
    case 53: host_item = 327681; break; // NOEXPR
    case 56: host_item = 262159; break; // CRNCYSTR
    default:
        if (v >= 7 && v <= 13)          // DAY_1..DAY_7
            host_item = 131079 + (v - 7);
        else if (v >= 14 && v <= 20)    // ABDAY_1..ABDAY_7
            host_item = 131072 + (v - 14);
        else if (v >= 21 && v <= 32)    // MON_1..MON_12
            host_item = 131098 + (v - 21);
        else if (v >= 33 && v <= 44)    // ABMON_1..ABMON_12
            host_item = 131086 + (v - 33);
        else {
            *found = 0;
            return (nl_item)0;
        }
        break;
    }
    return (nl_item)host_item;
#endif
}

static char *wrap_nl_langinfo(nl_item guest_item) {
    int found;
    nl_item host_item = guest_to_host_nl_item(guest_item, &found);
    if (!found) return "";
    return nl_langinfo(host_item);
}

// nl_langinfo_l() (#820) -- same canonical nl_item translation as
// nl_langinfo() above, against an explicit locale_t instead of the
// process-global/per-thread locale.
static char *wrap_nl_langinfo_l(nl_item guest_item, locale_t loc) {
    int found;
    nl_item host_item = guest_to_host_nl_item(guest_item, &found);
    if (!found) return "";
    return nl_langinfo_l(host_item, loc);
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

// #841 -- macOS libc's _strfmon() (see the "monetary.h" registration below)
// over-reads its own internal scratch allocation under AddressSanitizer:
// it mallocs a 15-byte buffer and then memcpy()s 9 bytes starting at offset
// 15 of it. Confirmed with a standalone 6-line clang -fsanitize=address
// program with zero CCCC involvement -- every conversion directive
// ("%n", "%i", "%!n", "%.2n", "%#5n") triggers it, a format with no
// directive doesn't. It's benign against a real allocator (15 bytes rounds
// up to a 16-byte malloc bucket, so the bytes read are mapped) and only
// aborts against ASan's exact-size redzones. Confirmed clean on
// Linux/glibc. Suppress just this frame under ASan rather than avoiding
// strfmon(): reimplementing it would go against the no-lossy-POSIX-
// emulation policy for a bug that's harmless outside ASan. A user's own
// ASAN_OPTIONS=suppressions=<file> merges with this list rather than
// replacing it (confirmed empirically), so this doesn't hide anything from
// someone actively debugging with a custom suppression file.
#if defined(__APPLE__) && (defined(__SANITIZE_ADDRESS__) || \
    (defined(__has_feature) && __has_feature(address_sanitizer)))
const char *__asan_default_suppressions(void) {
    return "interceptor_via_fun:_strfmon\n";
}
#endif

void register_posix_functions(VirtualMachine *vm) {
    // Blocking I/O — GIL released while blocked so other VM threads can run
    cc_register_cfunc(vm, "read",    (void*)wrap_read_gil,    3, 0);
    cc_register_cfunc(vm, "write",   (void*)wrap_write_gil,   3, 0);
    cc_register_cfunc(vm, "pread",   (void*)wrap_pread_gil,   4, 0);
    cc_register_cfunc(vm, "pwrite",  (void*)wrap_pwrite_gil,  4, 0);
    cc_register_cfunc(vm, "poll",    (void*)wrap_poll_gil,    3, 0);
#ifdef __linux__
    cc_register_cfunc(vm, "ppoll",   (void*)wrap_ppoll_gil,   4, 0);
#else
    // Non-atomic emulation (#824) -- only register if the caller opted in
    // via --posix-emulation; matches poll.h's declaration guard.
    if (vm->flags & CCCC_POSIX_EMULATION)
        cc_register_cfunc(vm, "ppoll", (void*)wrap_ppoll_gil, 4, 0);
#endif
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
    // readv/writev release the GIL like read/write/pread/pwrite.
    cc_register_cfunc(vm, "readv",    (void*)wrap_readv_gil,  3, 0);
    cc_register_cfunc(vm, "writev",   (void*)wrap_writev_gil, 3, 0);
    cc_register_cfunc(vm, "preadv",   (void*)wrap_preadv_gil,  4, 0);
    cc_register_cfunc(vm, "pwritev",  (void*)wrap_pwritev_gil, 4, 0);
    cc_register_variadic_cfunc(vm, "open",   (void*)wrap_open,  2, 0);
    cc_register_cfunc(vm, "creat",   (void*)wrap_creat, 2, 0);
    cc_register_variadic_cfunc(vm, "fcntl",  (void*)fcntl, 2, 0);
    // flock/ioctl/statfs/fstatfs: declared in include/ but never registered
    // anywhere -- same "undefined function" gap class as #783, surfaced by
    // widening tools/audit_ffi.py's header scan for #784/#792.
    cc_register_cfunc(vm, "flock",   (void*)flock,   2, 0);
    // ioctl (#795): request-code allowlist, not a raw passthrough. ioctl's
    // pointer argument is handed straight to the host syscall with no
    // translation, and unlike statfs/fstatfs (a fixed struct CCCC can
    // translate against) there is no bound on what a request code might
    // expect -- an unverified code risks the same guest/host struct-size
    // mismatch that statfs had before wrap_statfs below. wrap_ioctl only
    // forwards the handful of request codes whose guest/host argument
    // layout has been verified (see include/sys/ioctl.h); anything else
    // fails with -1/EINVAL unless the caller opted into raw passthrough via
    // --posix-emulation (same policy as ppoll/sched_* under #824).
    cc_register_variadic_cfunc(vm, "ioctl", (void*)wrap_ioctl, 2, 0);
    cc_register_cfunc(vm, "statfs",  (void*)wrap_statfs,  2, 0);
    cc_register_cfunc(vm, "fstatfs", (void*)wrap_fstatfs, 2, 0);
    cc_register_cfunc(vm, "statvfs",  (void*)wrap_statvfs,  2, 0);
    cc_register_cfunc(vm, "fstatvfs", (void*)wrap_fstatvfs, 2, 0);
    cc_register_cfunc(vm, "sched_yield", (void*)sched_yield, 0, 0);
    cc_register_cfunc(vm, "sched_get_priority_min", (void*)wrap_sched_get_priority_min, 1, 0);
    cc_register_cfunc(vm, "sched_get_priority_max", (void*)wrap_sched_get_priority_max, 1, 0);
#ifdef __linux__
    cc_register_cfunc(vm, "sched_setparam", (void*)wrap_sched_setparam, 2, 0);
    cc_register_cfunc(vm, "sched_getparam", (void*)wrap_sched_getparam, 2, 0);
    cc_register_cfunc(vm, "sched_setscheduler", (void*)wrap_sched_setscheduler, 3, 0);
    cc_register_cfunc(vm, "sched_getscheduler", (void*)wrap_sched_getscheduler, 1, 0);
    cc_register_cfunc(vm, "sched_rr_get_interval", (void*)wrap_sched_rr_get_interval, 2, 0);
#else
    // No process-scheduling API on this host at all (#824) -- only register
    // the always-ENOSYS stubs if the caller opted in via --posix-emulation;
    // matches sched.h's declaration guard.
    if (vm->flags & CCCC_POSIX_EMULATION) {
        cc_register_cfunc(vm, "sched_setparam", (void*)wrap_sched_setparam, 2, 0);
        cc_register_cfunc(vm, "sched_getparam", (void*)wrap_sched_getparam, 2, 0);
        cc_register_cfunc(vm, "sched_setscheduler", (void*)wrap_sched_setscheduler, 3, 0);
        cc_register_cfunc(vm, "sched_getscheduler", (void*)wrap_sched_getscheduler, 1, 0);
        cc_register_cfunc(vm, "sched_rr_get_interval", (void*)wrap_sched_rr_get_interval, 2, 0);
    }
#endif
    cc_register_cfunc(vm, "posix_spawnattr_init", (void*)wrap_posix_spawnattr_init, 1, 0);
    cc_register_cfunc(vm, "posix_spawnattr_destroy", (void*)wrap_posix_spawnattr_destroy, 1, 0);
    cc_register_cfunc(vm, "posix_spawnattr_getflags", (void*)wrap_posix_spawnattr_getflags, 2, 0);
    cc_register_cfunc(vm, "posix_spawnattr_setflags", (void*)wrap_posix_spawnattr_setflags, 2, 0);
    cc_register_cfunc(vm, "posix_spawnattr_getpgroup", (void*)wrap_posix_spawnattr_getpgroup, 2, 0);
    cc_register_cfunc(vm, "posix_spawnattr_setpgroup", (void*)wrap_posix_spawnattr_setpgroup, 2, 0);
    cc_register_cfunc(vm, "posix_spawnattr_getsigdefault", (void*)wrap_posix_spawnattr_getsigdefault, 2, 0);
    cc_register_cfunc(vm, "posix_spawnattr_setsigdefault", (void*)wrap_posix_spawnattr_setsigdefault, 2, 0);
    cc_register_cfunc(vm, "posix_spawnattr_getsigmask", (void*)wrap_posix_spawnattr_getsigmask, 2, 0);
    cc_register_cfunc(vm, "posix_spawnattr_setsigmask", (void*)wrap_posix_spawnattr_setsigmask, 2, 0);
    cc_register_cfunc(vm, "posix_spawn_file_actions_init", (void*)wrap_posix_spawn_file_actions_init, 1, 0);
    cc_register_cfunc(vm, "posix_spawn_file_actions_destroy", (void*)wrap_posix_spawn_file_actions_destroy, 1, 0);
    cc_register_cfunc(vm, "posix_spawn_file_actions_addopen", (void*)wrap_posix_spawn_file_actions_addopen, 5, 0);
    cc_register_cfunc(vm, "posix_spawn_file_actions_addclose", (void*)wrap_posix_spawn_file_actions_addclose, 2, 0);
    cc_register_cfunc(vm, "posix_spawn_file_actions_adddup2", (void*)wrap_posix_spawn_file_actions_adddup2, 3, 0);
    cc_register_cfunc(vm, "posix_spawn", (void*)wrap_posix_spawn_gil, 6, 0);
    cc_register_cfunc(vm, "posix_spawnp", (void*)wrap_posix_spawnp_gil, 6, 0);
    cc_register_cfunc(vm, "iconv_open",  (void*)iconv_open,  2, 0);
    cc_register_cfunc(vm, "iconv",       (void*)iconv,       5, 0);
    cc_register_cfunc(vm, "iconv_close", (void*)iconv_close, 1, 0);
    cc_register_cfunc(vm, "nl_langinfo", (void*)wrap_nl_langinfo, 1, 0);
    cc_register_cfunc(vm, "nl_langinfo_l", (void*)wrap_nl_langinfo_l, 2, 0);
    cc_register_cfunc(vm, "catopen",  (void*)catopen,  2, 0);
    cc_register_cfunc(vm, "catgets",  (void*)catgets,  4, 0);
    cc_register_cfunc(vm, "catclose", (void*)catclose, 1, 0);
    cc_register_cfunc(vm, "hcreate",  (void*)hcreate,  1, 0);
    cc_register_cfunc(vm, "hdestroy", (void*)hdestroy, 0, 0);
    cc_register_cfunc(vm, "hsearch",  (void*)wrap_hsearch, 2, 0);
    cc_register_cfunc(vm, "insque",   (void*)insque,   2, 0);
    cc_register_cfunc(vm, "remque",   (void*)remque,   1, 0);
    cc_register_cfunc(vm, "lfind",    (void*)wrap_lfind,   5, 0);
    cc_register_cfunc(vm, "lsearch",  (void*)wrap_lsearch, 5, 0);
    cc_register_cfunc(vm, "tsearch",  (void*)wrap_tsearch, 3, 0);
    cc_register_cfunc(vm, "tfind",    (void*)wrap_tfind,   3, 0);
    cc_register_cfunc(vm, "tdelete",  (void*)wrap_tdelete, 3, 0);
    cc_register_cfunc(vm, "twalk",    (void*)wrap_twalk,   2, 0);

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
    // preadv2/pwritev2 (#793): declared+wrapped above, next to preadv/pwritev.
    cc_register_cfunc(vm, "preadv2",  (void*)wrap_preadv2_gil,  5, 0);
    cc_register_cfunc(vm, "pwritev2", (void*)wrap_pwritev2_gil, 5, 0);
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

    // monetary.h (#808) -- strfmon is variadic with double arguments, but
    // (like syslog above) is a real, non-va_list-forwarding call site, so
    // the plain host function registers directly and codegen's per-call-
    // site double_arg_mask threads the double through correctly -- no
    // split-format host-side reimplementation needed (confirmed
    // empirically: a real double argument round-trips through a "%n"
    // conversion correctly). On macOS, this host strfmon() has an internal
    // over-read that only ASan notices -- see the __asan_default_suppressions
    // hook above (#841).
    cc_register_variadic_cfunc(vm, "strfmon", (void*)strfmon, 3, 0);
    // strfmon_l (#820) -- locale_t sits before the format string, so this
    // is 4 fixed args, not 3 like strfmon above.
    cc_register_variadic_cfunc(vm, "strfmon_l", (void*)strfmon_l, 4, 0);

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

    // fts.h (#811)
    cc_register_cfunc(vm, "fts_open",     (void*)wrap_fts_open,     3, 0);
    cc_register_cfunc(vm, "fts_read",     (void*)wrap_fts_read,     1, 0);
    cc_register_cfunc(vm, "fts_children", (void*)wrap_fts_children, 2, 0);
    cc_register_cfunc(vm, "fts_set",      (void*)wrap_fts_set,      3, 0);
    cc_register_cfunc(vm, "fts_close",    (void*)wrap_fts_close,    1, 0);

    // sys/ipc.h, sys/shm.h, sys/sem.h, sys/msg.h (#812)
    cc_register_cfunc(vm, "ftok",    (void*)wrap_ftok,    2, 0);
    cc_register_cfunc(vm, "shmget",  (void*)wrap_shmget,  3, 0);
    cc_register_cfunc(vm, "shmat",   (void*)wrap_shmat,   3, 0);
    cc_register_cfunc(vm, "shmdt",   (void*)wrap_shmdt,   1, 0);
    cc_register_cfunc(vm, "shmctl",  (void*)wrap_shmctl,  3, 0);
    cc_register_cfunc(vm, "semget",  (void*)wrap_semget,  3, 0);
    cc_register_cfunc(vm, "semop",   (void*)wrap_semop,   3, 0);
    cc_register_variadic_cfunc(vm, "semctl", (void*)wrap_semctl, 3, 0);
    cc_register_cfunc(vm, "msgget",  (void*)wrap_msgget,  2, 0);
    cc_register_cfunc(vm, "msgsnd",  (void*)wrap_msgsnd,  4, 0);
    cc_register_cfunc(vm, "msgrcv",  (void*)wrap_msgrcv,  5, 0);
    cc_register_cfunc(vm, "msgctl",  (void*)wrap_msgctl,  3, 0);

    // wordexp.h (#802)
    cc_register_cfunc(vm, "wordexp",  (void*)wrap_wordexp,  3, 0);
    cc_register_cfunc(vm, "wordfree", (void*)wrap_wordfree, 1, 0);

    // aio.h (#804)
    cc_register_cfunc(vm, "aio_read",    (void*)wrap_aio_read,    1, 0);
    cc_register_cfunc(vm, "aio_write",   (void*)wrap_aio_write,   1, 0);
    cc_register_cfunc(vm, "aio_error",   (void*)wrap_aio_error,   1, 0);
    cc_register_cfunc(vm, "aio_return",  (void*)wrap_aio_return,  1, 0);
    cc_register_cfunc(vm, "aio_cancel",  (void*)wrap_aio_cancel,  2, 0);
    cc_register_cfunc(vm, "aio_fsync",   (void*)wrap_aio_fsync,   2, 0);
    cc_register_cfunc(vm, "aio_suspend", (void*)wrap_aio_suspend, 3, 0);
    cc_register_cfunc(vm, "lio_listio",  (void*)wrap_lio_listio,  4, 0);

#ifdef __linux__
    // mqueue.h (#805) -- Linux-only, see include/mqueue.h
    cc_register_variadic_cfunc(vm, "mq_open", (void*)wrap_mq_open, 2, 0);
    cc_register_cfunc(vm, "mq_close",         (void*)wrap_mq_close,         1, 0);
    cc_register_cfunc(vm, "mq_unlink",        (void*)wrap_mq_unlink,        1, 0);
    cc_register_cfunc(vm, "mq_send",          (void*)wrap_mq_send,          4, 0);
    cc_register_cfunc(vm, "mq_receive",       (void*)wrap_mq_receive,       4, 0);
    cc_register_cfunc(vm, "mq_timedsend",     (void*)wrap_mq_timedsend,     5, 0);
    cc_register_cfunc(vm, "mq_timedreceive",  (void*)wrap_mq_timedreceive,  5, 0);
    cc_register_cfunc(vm, "mq_notify",        (void*)wrap_mq_notify,        2, 0);
    cc_register_cfunc(vm, "mq_setattr",       (void*)wrap_mq_setattr,       3, 0);
    cc_register_cfunc(vm, "mq_getattr",       (void*)wrap_mq_getattr,       2, 0);
#endif

#if defined(__APPLE__) || defined(CCCC_HAS_NDBM)
    // ndbm.h (#810, #871) -- macOS/BSD natively, Linux when built with
    // CCCC_HAS_NDBM=1, see include/ndbm.h. The five by-value-datum entry
    // points are registered under their internal __cccc_dbm_* names,
    // matched to include/ndbm.h's extern declarations for the
    // static-inline shims to call. dbm_clearerr is registered as
    // wrap_dbm_clearerr rather than a raw pass-through since gdbm's
    // version returns void, not POSIX's int.
    cc_register_cfunc(vm, "dbm_open",           (void*)wrap_dbm_open,     3, 0);
    cc_register_cfunc(vm, "dbm_close",          (void*)wrap_dbm_close,    1, 0);
    cc_register_cfunc(vm, "dbm_error",          (void*)dbm_error,         1, 0);
    cc_register_cfunc(vm, "dbm_clearerr",       (void*)wrap_dbm_clearerr, 1, 0);
    cc_register_cfunc(vm, "__cccc_dbm_fetch",   (void*)wrap_dbm_fetch,    5, 0);
    cc_register_cfunc(vm, "__cccc_dbm_firstkey",(void*)wrap_dbm_firstkey, 3, 0);
    cc_register_cfunc(vm, "__cccc_dbm_nextkey", (void*)wrap_dbm_nextkey,  3, 0);
    cc_register_cfunc(vm, "__cccc_dbm_delete",  (void*)wrap_dbm_delete,   3, 0);
    cc_register_cfunc(vm, "__cccc_dbm_store",   (void*)wrap_dbm_store,    6, 0);
#endif
}
#else
void register_posix_functions(VirtualMachine *vm) {
    (void)vm;
}
#endif
