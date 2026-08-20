// posix_poll.c -- poll/ppoll/select/pselect, pollfd bit translation and
// marshalling, and the macOS ppoll() emulation (#946 split of posix.c).
#include "posix_util.h"

#if !defined(_WIN32) && !defined(_WIN64)

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
    if (guest_events & 0x0040)
        host |= POLLRDNORM;
    if (guest_events & 0x0080)
        host |= POLLRDBAND;
    if (guest_events & 0x0100)
        host |= POLLWRNORM;
    if (guest_events & 0x0200)
        host |= POLLWRBAND;
    return host;
#else
    return guest_events;
#endif
}

static short host_to_guest_pollev(short host_revents) {
#ifdef __APPLE__
    short guest = host_revents &
                  (short)~(POLLRDNORM | POLLRDBAND | POLLWRNORM | POLLWRBAND);
    if (host_revents & POLLRDNORM)
        guest |= 0x0040;
    if (host_revents & POLLRDBAND)
        guest |= 0x0080;
    // POLLWRNORM aliases POLLOUT (0x0004) on macOS, so a host POLLOUT bit
    // sets both the canonical POLLOUT and POLLWRNORM guest bits -- this is
    // intentional and documented, not a bug: a caller polling for
    // POLLWRNORM alone still observes it set when writable.
    if (host_revents & POLLWRNORM)
        guest |= 0x0100;
    if (host_revents & POLLWRBAND)
        guest |= 0x0200;
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
    struct pollfd *host =
        (nfds <= POLL_STACK_NFDS)
            ? stack_buf
            : (struct pollfd *)malloc(sizeof(struct pollfd) * nfds);
    if (!host) {
        errno = ENOMEM;
        return -1;
    }
    for (nfds_t i = 0; i < nfds; i++) {
        host[i].fd      = guest_fds[i].fd;
        host[i].events  = guest_to_host_pollev(guest_fds[i].events);
        host[i].revents = 0;
    }
    *out_host = host;
    return 0;
}

static void poll_marshal_out(struct pollfd *host, struct pollfd *guest_fds,
                             nfds_t nfds) {
    for (nfds_t i = 0; i < nfds; i++)
        guest_fds[i].revents = host_to_guest_pollev(host[i].revents);
}

static long long wrap_poll_gil(long long fds, long long nfds,
                               long long timeout) {
    struct pollfd  stack_buf[POLL_STACK_NFDS];
    struct pollfd *host_fds;
    if (poll_marshal_in((struct pollfd *)fds, (nfds_t)nfds, stack_buf,
                        &host_fds) < 0)
        return -1;

    VirtualMachine *vm = cccc_posix_current_vm();
    int             r;
    if (!vm || !vm->gil_initialized) {
        r = poll(host_fds, (nfds_t)nfds, (int)timeout);
    } else {
        ExecState state;
        cccc_posix_save_and_release_gil(vm, &state);
        r = poll(host_fds, (nfds_t)nfds, (int)timeout);
        cccc_posix_acquire_and_restore_gil(vm, &state);
    }

    poll_marshal_out(host_fds, (struct pollfd *)fds, (nfds_t)nfds);
    if (host_fds != stack_buf)
        free(host_fds);
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
    int      have_old = 0;
    if (host_set_ptr) {
        if (pthread_sigmask(SIG_SETMASK, host_set_ptr, &old_set) == 0)
            have_old = 1;
    }
    int r           = poll(host_fds, nfds, ms);
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
static long long wrap_select_gil(long long nfds, long long readfds,
                                 long long writefds, long long exceptfds,
                                 long long timeout) {
    VirtualMachine *vm = cccc_posix_current_vm();
    if (!vm || !vm->gil_initialized)
        return (long long)select((int)nfds, (fd_set *)readfds,
                                 (fd_set *)writefds, (fd_set *)exceptfds,
                                 (struct timeval *)timeout);
    ExecState state;
    cccc_posix_save_and_release_gil(vm, &state);
    int r = select((int)nfds, (fd_set *)readfds, (fd_set *)writefds,
                   (fd_set *)exceptfds, (struct timeval *)timeout);
    cccc_posix_acquire_and_restore_gil(vm, &state);
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

static long long wrap_pselect_gil(long long nfds, long long readfds,
                                  long long writefds, long long exceptfds,
                                  long long timeout, long long sigmask) {
    sigset_t  host_set;
    sigset_t *host_set_ptr = NULL;
    if (sigmask) {
        cccc_posix_guest_sigset_to_host(*(unsigned int *)sigmask, &host_set);
        host_set_ptr = &host_set;
    }
    VirtualMachine *vm = cccc_posix_current_vm();
    if (!vm || !vm->gil_initialized)
        return (long long)pselect((int)nfds, (fd_set *)readfds,
                                  (fd_set *)writefds, (fd_set *)exceptfds,
                                  (const struct timespec *)timeout,
                                  host_set_ptr);
    ExecState state;
    cccc_posix_save_and_release_gil(vm, &state);
    int r = pselect((int)nfds, (fd_set *)readfds, (fd_set *)writefds,
                    (fd_set *)exceptfds, (const struct timespec *)timeout,
                    host_set_ptr);
    cccc_posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

// ppoll() (#821) -- the poll()-with-timeout-and-sigmask analog of
// pselect(). The guest sigset_t is translated to a real host sigset_t via
// the same cccc_posix_guest_sigset_to_host() pselect() uses above.
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
static long long wrap_ppoll_gil(long long fds, long long nfds,
                                long long timeout, long long sigmask) {
    struct pollfd  stack_buf[POLL_STACK_NFDS];
    struct pollfd *host_fds;
    if (poll_marshal_in((struct pollfd *)fds, (nfds_t)nfds, stack_buf,
                        &host_fds) < 0)
        return -1;

    sigset_t  host_set;
    sigset_t *host_set_ptr = NULL;
    if (sigmask) {
        cccc_posix_guest_sigset_to_host(*(unsigned int *)(void *)sigmask,
                                        &host_set);
        host_set_ptr = &host_set;
    }

    VirtualMachine *vm = cccc_posix_current_vm();
    int             r;
#ifdef __linux__
    // ppoll is a glibc extension gated behind __USE_GNU, which the host
    // <poll.h>/<sys/poll.h> only exposes under _GNU_SOURCE -- same gap
    // class as mremap/fallocate/splice above, so forward-declared locally
    // rather than flipping on _GNU_SOURCE for the whole TU. glibc still
    // exports the symbol regardless.
    extern int ppoll(struct pollfd * fds, nfds_t nfds,
                     const struct timespec *timeout, const sigset_t *sigmask);
    if (!vm || !vm->gil_initialized) {
        r = ppoll(host_fds, (nfds_t)nfds, (const struct timespec *)timeout,
                  host_set_ptr);
    } else {
        ExecState state;
        cccc_posix_save_and_release_gil(vm, &state);
        r = ppoll(host_fds, (nfds_t)nfds, (const struct timespec *)timeout,
                  host_set_ptr);
        cccc_posix_acquire_and_restore_gil(vm, &state);
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
        ms                 = (int)((total_ns + 999999) / 1000000);
        if (ms < 0)
            ms = 0;
    }

    if (!vm || !vm->gil_initialized) {
        r = ppoll_emulate_macos(host_fds, (nfds_t)nfds, ms, host_set_ptr);
    } else {
        ExecState state;
        cccc_posix_save_and_release_gil(vm, &state);
        r = ppoll_emulate_macos(host_fds, (nfds_t)nfds, ms, host_set_ptr);
        cccc_posix_acquire_and_restore_gil(vm, &state);
    }
#endif

    poll_marshal_out(host_fds, (struct pollfd *)fds, (nfds_t)nfds);
    if (host_fds != stack_buf)
        free(host_fds);
    return (long long)r;
}

void register_posix_poll_functions(VirtualMachine *vm) {
    cc_register_cfunc(vm, "poll", (void *)wrap_poll_gil, 3, 0);
#ifdef __linux__
    cc_register_cfunc(vm, "ppoll", (void *)wrap_ppoll_gil, 4, 0);
#else
    // Non-atomic emulation (#824) -- only register if the caller opted in
    // via --posix-emulation; matches poll.h's declaration guard.
    if (vm->flags & CCCC_POSIX_EMULATION)
        cc_register_cfunc(vm, "ppoll", (void *)wrap_ppoll_gil, 4, 0);
#endif
    cc_register_cfunc(vm, "select", (void *)wrap_select_gil, 5, 0);
    cc_register_cfunc(vm, "pselect", (void *)wrap_pselect_gil, 6, 0);
}

#else
void register_posix_poll_functions(VirtualMachine *vm) {
    (void)vm;
}
#endif
