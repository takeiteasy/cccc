// posix_aio.c -- SIGEV_THREAD cookie machinery (shared with
// posix_mqueue.c) plus aio.h (#946 split of posix.c).
#include "posix_util.h"

#if !defined(_WIN32) && !defined(_WIN64)

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
// struct sigevent comment) is honored via cccc_posix_sigevent_prepare() above, which
// swaps in cccc_sigev_thread_trampoline() as the real host notification
// function; SIGEV_NONE/SIGEV_SIGNAL pass through unchanged.
int cccc_posix_sigevent_prepare(struct sigevent *sev) {
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
    if (!cccc_posix_sigevent_prepare(&cb->aio_sigevent))
        return -1;
    return (long long)aio_read(cb);
}

static long long wrap_aio_write(long long aiocbp) {
    struct aiocb *cb = (struct aiocb *)(intptr_t)aiocbp;
    if (!cb) {
        errno = EINVAL;
        return -1;
    }
    if (!cccc_posix_sigevent_prepare(&cb->aio_sigevent))
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
    // in aio_sigevent.sigev_value by cccc_posix_sigevent_prepare()) is freed here since
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
    if (!cccc_posix_sigevent_prepare(&cb->aio_sigevent))
        return -1;
    return (long long)aio_fsync((int)op, cb);
}

static long long wrap_aio_suspend(long long aiocblist, long long nent, long long timeoutp) {
    VirtualMachine *vm = cccc_posix_current_vm();
    const struct aiocb *const *list = (const struct aiocb *const *)(intptr_t)aiocblist;
    const struct timespec *tp = (const struct timespec *)(intptr_t)timeoutp;
    if (!vm || !vm->gil_initialized)
        return (long long)aio_suspend(list, (int)nent, tp);
    ExecState state;
    cccc_posix_save_and_release_gil(vm, &state);
    int r = aio_suspend(list, (int)nent, tp);
    cccc_posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

static long long wrap_lio_listio(long long mode, long long aiocblist, long long nent, long long sigp) {
    struct sigevent *sev = (struct sigevent *)(intptr_t)sigp;
    if (!cccc_posix_sigevent_prepare(sev))
        return -1;
    struct aiocb *const *list = (struct aiocb *const *)(intptr_t)aiocblist;
    // Each list entry carries its own per-request aio_sigevent (LIO_NOP
    // entries are skipped by aio_lio_opcode; the pointer itself must still
    // be non-NULL to check, matching the real lio_listio() contract).
    for (long long i = 0; i < nent; i++) {
        if (!list[i])
            continue;
        if (!cccc_posix_sigevent_prepare(&list[i]->aio_sigevent))
            return -1;
    }
    VirtualMachine *vm = cccc_posix_current_vm();
    if (!vm || !vm->gil_initialized || (int)mode != LIO_WAIT)
        return (long long)lio_listio((int)mode, list, (int)nent, sev);
    ExecState state;
    cccc_posix_save_and_release_gil(vm, &state);
    int r = lio_listio((int)mode, list, (int)nent, sev);
    cccc_posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

void register_posix_aio_functions(VirtualMachine *vm) {
    // aio.h (#804)
    cc_register_cfunc(vm, "aio_read",    (void*)wrap_aio_read,    1, 0);
    cc_register_cfunc(vm, "aio_write",   (void*)wrap_aio_write,   1, 0);
    cc_register_cfunc(vm, "aio_error",   (void*)wrap_aio_error,   1, 0);
    cc_register_cfunc(vm, "aio_return",  (void*)wrap_aio_return,  1, 0);
    cc_register_cfunc(vm, "aio_cancel",  (void*)wrap_aio_cancel,  2, 0);
    cc_register_cfunc(vm, "aio_fsync",   (void*)wrap_aio_fsync,   2, 0);
    cc_register_cfunc(vm, "aio_suspend", (void*)wrap_aio_suspend, 3, 0);
    cc_register_cfunc(vm, "lio_listio",  (void*)wrap_lio_listio,  4, 0);
}

#else
void register_posix_aio_functions(VirtualMachine *vm) { (void)vm; }
#endif
