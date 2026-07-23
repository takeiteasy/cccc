// signal.h stdlib — VM-managed signal handling
#include "../cccc.h"
#include "../internal.h"
#include <errno.h>
#include <signal.h>

/* Global async-safe pending flags (written only by native signal shims) */
volatile sig_atomic_t _cccc_pending[CCCC_NSIG];
volatile sig_atomic_t _cccc_any_pending;

/* Minimal async-safe shim: only sets pending flags, never touches VM state */
void _cccc_sig_shim(int sig) {
    if (sig > 0 && sig < CCCC_NSIG) {
        _cccc_pending[sig] = 1;
        _cccc_any_pending  = 1;
    }
}

// ---------------------------------------------------------------------------
// sigaction() (#738)
//
// Was previously a raw passthrough to the real host sigaction(), which
// crashed the moment a delivered signal invoked the guest's raw
// function-pointer value as machine code (the same crash class as
// scandir's callbacks, but worse: it can fire asynchronously, outside GIL
// control, at any point). Fixed by reusing the exact mechanism op_VSIGNAL_fn
// (ops.c) already uses for the simpler signal() API: populate
// vm->vm_sigslots[sig] and install the async-safe _cccc_sig_shim as the
// real OS-level handler via cccc_set_guest_signal_action. The dispatch
// loop's pending-signal poll (vm.c, cccc_vm_eval_dispatch) reads
// vm_sigslots generically -- it does not care whether a slot was populated
// by VSIGNAL or here, so no codegen changes are needed.
//
// GuestSigaction mirrors include/signal.h's struct sigaction layout
// exactly: { void (*sa_handler)(int); sigset_t sa_mask; int sa_flags; }
// with sigset_t = unsigned int. This is the GUEST's own self-consistent
// layout, not the host's real struct sigaction (which has a wildly
// different sa_mask width -- see the sigset_t note below -- plus a
// sa_sigaction union member this guest header doesn't expose at all, so
// SA_SIGINFO can't even be requested through this API). wrap_sigaction
// never hands this pointer to the real host sigaction(); it only extracts
// field values, so host-ABI compatibility is irrelevant here.
// Field names deliberately avoid sa_handler/sa_mask/sa_flags: on BSD-derived
// <signal.h> (this host included), sa_handler is a macro aliasing a union
// member of the REAL struct sigaction, and would rewrite a same-named field
// in this unrelated struct before the compiler ever sees it as an
// identifier.
typedef struct {
    void (*g_handler)(int);
    unsigned int g_mask;
    int g_flags;
} GuestSigaction;

static long long wrap_sigaction(long long sig, long long actp, long long oactp) {
    VirtualMachine *vm = cccc_current_ffi_vm();
    if (!vm || sig <= 0 || sig >= CCCC_NSIG) {
        errno = EINVAL;
        return -1;
    }
#if defined(SIGKILL) && defined(SIGSTOP)
    if (sig == SIGKILL || sig == SIGSTOP) {
        errno = EINVAL;
        return -1;
    }
#endif

    SigSlot *slot = &vm->vm_sigslots[sig];

    if (oactp) {
        GuestSigaction *oact = (GuestSigaction *)oactp;
        if (slot->action == 1)
            oact->g_handler = (void (*)(int))(long long)1; /* SIG_IGN */
        else if (slot->action == 2)
            oact->g_handler = (void (*)(int))slot->handler_fn;
        else
            oact->g_handler = (void (*)(int))(long long)0; /* SIG_DFL */
        oact->g_mask = slot->sa_mask;
        oact->g_flags = slot->sa_flags;
    }

    if (!actp)
        return 0; /* query-only */

    GuestSigaction *act = (GuestSigaction *)actp;
    long long handler = (long long)act->g_handler;

    int new_action = (handler == 0) ? 0 : (handler == 1) ? 1 : 2;
    slot->action = new_action;
    slot->handler_fn = (new_action == 2) ? handler : 0;
    slot->sa_mask = act->g_mask;
    slot->sa_flags = act->g_flags;

    if (cccc_set_guest_signal_action(vm, (int)sig, new_action) != 0) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// sigset_t operations (#738 -- unrelated OOB-write bug found while fixing
// sigaction)
//
// include/signal.h defines sigset_t as `unsigned int` (a 32-bit bitmask,
// covering signals 1..31 -- CCCC_NSIG's whole range). These were previously
// raw passthroughs to the real host sigemptyset/sigfillset/sigaddset/
// sigdelset/sigismember, which coincidentally worked on macOS (whose real
// sigset_t is also a 4-byte unsigned int) but is a genuine out-of-bounds
// write/read on Linux, where the real sigset_t is a 128-byte struct: the
// host functions would write/read 128 bytes into/from a buffer the guest
// only ever allocated as 4. Reimplemented natively against the guest's own
// 4-byte representation instead of touching the host's sigset_t at all.
static long long wrap_sigemptyset(long long set) {
    if (!set) { errno = EINVAL; return -1; }
    *(unsigned int *)set = 0;
    return 0;
}

static long long wrap_sigfillset(long long set) {
    if (!set) { errno = EINVAL; return -1; }
    *(unsigned int *)set = 0xFFFFFFFFu;
    return 0;
}

static long long wrap_sigaddset(long long set, long long signo) {
    if (!set || signo <= 0 || signo >= CCCC_NSIG) { errno = EINVAL; return -1; }
    *(unsigned int *)set |= (1u << (unsigned)(signo - 1));
    return 0;
}

static long long wrap_sigdelset(long long set, long long signo) {
    if (!set || signo <= 0 || signo >= CCCC_NSIG) { errno = EINVAL; return -1; }
    *(unsigned int *)set &= ~(1u << (unsigned)(signo - 1));
    return 0;
}

static long long wrap_sigismember(long long set, long long signo) {
    if (!set || signo <= 0 || signo >= CCCC_NSIG) { errno = EINVAL; return -1; }
    return (*(unsigned int *)set & (1u << (unsigned)(signo - 1))) ? 1 : 0;
}

void register_signal_functions(VirtualMachine *vm) {
    /* signal() and raise() are handled via VSIGNAL/VRAISE opcodes;
       no FFI registration is needed */
    cc_register_cfunc(vm, "sigaction",   (void*)wrap_sigaction,   3, 0);
    cc_register_cfunc(vm, "sigemptyset", (void*)wrap_sigemptyset, 1, 0);
    cc_register_cfunc(vm, "sigfillset",  (void*)wrap_sigfillset,  1, 0);
    cc_register_cfunc(vm, "sigaddset",   (void*)wrap_sigaddset,   2, 0);
    cc_register_cfunc(vm, "sigdelset",   (void*)wrap_sigdelset,   2, 0);
    cc_register_cfunc(vm, "sigismember", (void*)wrap_sigismember, 2, 0);
}
