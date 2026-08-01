// signal.h stdlib — VM-managed signal handling
#include "../cccc.h"
#include "../internal.h"
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <unistd.h>

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
// sa_sigaction / SA_SIGINFO (#745)
//
// Adds the three-argument signal handler form registerable via sigaction()'s
// SA_SIGINFO flag. The dispatch loop's pending-signal poll (src/vm.c) and
// op_VRAISE_fn (src/ops.c) both check slot->sa_flags & SA_SIGINFO before
// entering a guest handler and, if set, additionally populate REG_A1 with a
// pointer to a guest-layout siginfo_t and REG_A2 with a (currently
// unmodelled) ucontext pointer.
//
// GuestSiginfo mirrors include/signal.h's per-platform siginfo_t layout
// exactly -- same reasoning as GuestSigaction below: this is the GUEST's
// own self-consistent struct, not the host's real siginfo_t (a much larger
// union-based struct on both platforms). The two delivery paths populate it
// differently:
//   - the async host-signal path (_cccc_sig_shim_info, installed as the
//     real OS-level handler) captures the real si_code/si_pid/si_uid/
//     si_status/si_errno from the host's siginfo_t at the moment of
//     delivery, since that's genuine kernel-provided data (e.g. a real
//     SIGCHLD's si_code == CLD_EXITED). si_status is only meaningful for
//     SIGCHLD -- for other signals it's whatever the host's own
//     union-based siginfo_t happens to hold at that offset.
//   - raise() (VRAISE, ops.c) never goes through the host signal mechanism
//     at all, so it synthesizes real POSIX raise() semantics instead:
//     si_code = SI_USER, si_pid = getpid(), si_uid = getuid().
// Storage is a fixed-size, host-static array indexed by signal number --
// deliberately not stack/heap allocated: a guest deref of a host-static
// pointer is addressable under every CCCC safety tier (-0..-3; the
// existing gethostbyname()/hostent path already proves this for FFI-
// returned host-static pointers, and this is the same class of pointer).
// Field names si_pid/si_uid/si_status deliberately avoided (g_pid/g_uid/
// g_status instead): on glibc, si_pid/si_uid/si_status are macros aliasing
// members of the REAL siginfo_t's anonymous union (_sifields._kill.si_pid
// etc.), and would rewrite same-named fields in this unrelated struct
// before the compiler ever sees them as identifiers -- the same class of
// bug the GuestSigaction comment below warns about for sa_handler.
#ifdef __APPLE__
typedef struct {
    int si_signo;
    int si_errno;
    int si_code;
    int g_pid;
    int g_uid;
    int g_status;
    char __si_pad[104 - 6 * (int)sizeof(int)];
} GuestSiginfo;
#else
typedef struct {
    int si_signo;
    int si_errno;
    int si_code;
    int __si_pad0;
    int g_pid;
    int g_uid;
    int g_status;
    char __si_pad[128 - 7 * (int)sizeof(int)];
} GuestSiginfo;
#endif

static GuestSiginfo guest_siginfo[CCCC_NSIG];

/* Async-safe captured siginfo fields, written only from _cccc_sig_shim_info
   (real OS signal context) and read back by cccc_guest_siginfo_for from the
   dispatch loop. Plain sig_atomic_t arrays, same design as _cccc_pending --
   safe because the dispatch loop only consumes a signal's captured fields
   after observing _cccc_pending[sig] set, at which point delivery for that
   signal has already completed. */
static volatile sig_atomic_t _cccc_si_code[CCCC_NSIG];
static volatile sig_atomic_t _cccc_si_pid[CCCC_NSIG];
static volatile sig_atomic_t _cccc_si_uid[CCCC_NSIG];
static volatile sig_atomic_t _cccc_si_status[CCCC_NSIG];
static volatile sig_atomic_t _cccc_si_errno[CCCC_NSIG];

/* Async-safe shim for SA_SIGINFO handlers: same pending-flag contract as
   _cccc_sig_shim, plus captures the host-provided siginfo_t fields for
   cccc_guest_siginfo_for to materialize later. */
void _cccc_sig_shim_info(int sig, siginfo_t *info, void *ucontext) {
    (void)ucontext;
    if (sig > 0 && sig < CCCC_NSIG) {
        if (info) {
            _cccc_si_code[sig]   = info->si_code;
            _cccc_si_pid[sig]    = info->si_pid;
            _cccc_si_uid[sig]    = info->si_uid;
            _cccc_si_status[sig] = info->si_status;
            _cccc_si_errno[sig]  = info->si_errno;
        }
        _cccc_pending[sig] = 1;
        _cccc_any_pending  = 1;
    }
}

long long cccc_guest_siginfo_for(int sig, int synthesized) {
    if (sig <= 0 || sig >= CCCC_NSIG)
        return 0;
    GuestSiginfo *gi = &guest_siginfo[sig];
    gi->si_signo = sig;
    if (synthesized) {
        gi->si_errno = 0;
        gi->si_code  = SI_USER;
        gi->g_pid    = (int)getpid();
        gi->g_uid    = (int)getuid();
        gi->g_status = 0;
    } else {
        gi->si_errno = (int)_cccc_si_errno[sig];
        gi->si_code  = (int)_cccc_si_code[sig];
        gi->g_pid    = (int)_cccc_si_pid[sig];
        gi->g_uid    = (int)_cccc_si_uid[sig];
        gi->g_status = (int)_cccc_si_status[sig];
    }
    return (long long)(intptr_t)gi;
}

// ---------------------------------------------------------------------------
// sa_mask / SA_RESTART / SA_NODEFER / SA_RESETHAND enforcement (#787)
//
// #745 made the SA_* flag values correct per platform and enforced
// SA_SIGINFO at dispatch; sa_mask/SA_RESTART/SA_NODEFER/SA_RESETHAND were
// stored and round-tripped through oact faithfully but inert. This section
// enforces the first three; SA_RESTART is handled separately, by passing it
// through to the host sigaction() in cccc_set_guest_signal_action
// (src/host_signal.c) -- it's a kernel-level concern for blocking FFI
// syscalls, not something the VM dispatch loop can meaningfully emulate.
//
// See the SigFrame comment in cccc.h for why handler-return detection is an
// sp watermark rather than a dedicated opcode.
// #877: pushes a SigFrame carrying a full register-file snapshot, used for
// ASYNC delivery only (the dispatch loop's pending-signal poll and the
// SIGEV_THREAD poll, both in src/vm.c). Async delivery can land between
// ANY two bytecode instructions -- not just at a call boundary -- so
// unlike synchronous raise()/VRAISE (which runs at a call boundary, where
// the normal caller-saved ABI already protects the interrupted code) it
// needs its own save/restore, mirroring how a real hardware interrupt
// preserves the state it clobbers. Callers have already verified
// vm->sig_depth < CCCC_SIG_FRAME_MAX -- a full frame stack means "defer
// this delivery," decided by the caller in vm.c before pushing the return
// address or getting here at all. `sig` is the real signal number for the
// pending-signal poll, or 0 for a SIGEV_THREAD notification (there's no
// signal involved, just a callback function pointer).
static void push_async_frame(VirtualMachine *vm, int sig, unsigned int saved_blocked) {
    SigFrame *f = &vm->sig_frames[vm->sig_depth];
    f->sig = sig;
    f->saved_blocked = saved_blocked;
    f->sp_at_entry = vm->sp;
    f->saved_pc = vm->pc; // the interruption point -- also what was just
                          // pushed as the handler's return address
    f->save_slot = -1;

    if (!vm->async_reg_saves)
        vm->async_reg_saves = malloc(sizeof(AsyncRegSave) * CCCC_SIG_FRAME_MAX);
    if (vm->async_reg_saves) {
        AsyncRegSave *save = &vm->async_reg_saves[vm->sig_depth];
        memcpy(save->regs, vm->regs, sizeof(save->regs));
        memcpy(save->fregs, vm->fregs, sizeof(save->fregs));
        memcpy(save->vregs, vm->vregs, sizeof(save->vregs));
        f->save_slot = vm->sig_depth;
    }
    /* If the malloc failed, save_slot stays -1 -- the frame still tracks
       sig_blocked/sp_at_entry/saved_pc correctly, it just can't restore
       registers on return (fails safe: same exposure as before #877,
       not worse). */
    vm->sig_depth++;
}

// #877: SIGEV_THREAD poll (src/vm.c) has no SigSlot/sa_mask of its own to
// enforce, just needs the same register-snapshot protection as a real
// signal. saved_blocked is vm->sig_blocked unchanged, so the eventual
// unwind in cccc_signal_poll_handler_returns is a no-op for the mask.
void cccc_signal_push_async_frame(VirtualMachine *vm) {
    push_async_frame(vm, 0, vm->sig_blocked);
}

int cccc_signal_prepare_delivery(VirtualMachine *vm, int sig, SigSlot *slot, bool async) {
    int flags = slot->sa_flags;
    unsigned int bit = 1u << (unsigned)(sig - 1);
    unsigned int newly_blocked = slot->sa_mask;
    if (!(flags & SA_NODEFER)) newly_blocked |= bit;

    if (async) {
        // Caller (vm.c) already checked sig_depth < CCCC_SIG_FRAME_MAX.
        push_async_frame(vm, sig, vm->sig_blocked);
    } else if (vm->sig_depth < CCCC_SIG_FRAME_MAX) {
        SigFrame *f = &vm->sig_frames[vm->sig_depth++];
        f->sig = sig;
        f->saved_blocked = vm->sig_blocked;
        f->sp_at_entry = vm->sp;
        f->saved_pc = vm->pc;
        f->save_slot = -1; // sync: call-boundary ABI already protects the caller
    }
    /* If the frame stack overflowed (pathological recursive-signal case --
       sync path only; async never reaches here with a full stack, see
       above), still apply the block -- it just won't be automatically
       unblocked on return. Delivering unmasked would be worse (immediate
       re-entrancy). */
    vm->sig_blocked |= newly_blocked;

    if (flags & SA_RESETHAND) {
        slot->action = 0;
        slot->handler_fn = 0;
        slot->sa_flags = 0;
        slot->sa_mask = 0;
        cccc_set_guest_signal_action(vm, sig, 0);
    }
    return flags;
}

void cccc_signal_poll_handler_returns(VirtualMachine *vm) {
    while (vm->sig_depth > 0 &&
           vm->sp > vm->sig_frames[vm->sig_depth - 1].sp_at_entry) {
        vm->sig_depth--;
        SigFrame *f = &vm->sig_frames[vm->sig_depth];
        vm->sig_blocked = f->saved_blocked;
        // #877: only restore the register snapshot on a genuine handler
        // return -- sp landed exactly one past sp_at_entry (the pushed
        // return address was popped by a real LEV3) and pc landed back
        // exactly on the interruption point. A handler that longjmp()'d
        // out instead moves sp/pc in ways that generally won't satisfy
        // both, and op_LONGJMP_fn (ops.c) owns REG_A0 (the setjmp return
        // value) on that path -- restoring over it would corrupt it.
        if (f->save_slot >= 0 && vm->async_reg_saves &&
            vm->sp == f->sp_at_entry + 1 && vm->pc == f->saved_pc) {
            AsyncRegSave *save = &vm->async_reg_saves[f->save_slot];
            memcpy(vm->regs, save->regs, sizeof(save->regs));
            memcpy(vm->fregs, save->fregs, sizeof(save->fregs));
            memcpy(vm->vregs, save->vregs, sizeof(save->vregs));
        }
        /* A signal deferred while blocked may now be deliverable. */
        _cccc_any_pending = 1;
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
