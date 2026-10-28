/* Host-fault recovery for the VM dispatch loop (ticket #455). */
#if !defined(_WIN32) && !defined(_WIN64)
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#endif

#include "internal.h"

#if !defined(_WIN32) && !defined(_WIN64)

typedef struct HostSignalGuard HostSignalGuard;
struct HostSignalGuard {
    sigjmp_buf            env;
    VirtualMachine       *vm;
    HostSignalGuard      *previous;
    volatile sig_atomic_t signal;
    volatile sig_atomic_t guest_resume;
    void *volatile fault_addr;
    const char *volatile native; // native call the fault interrupted, or NULL
};

static const int host_fault_signals[] = {
    SIGSEGV,
#ifdef SIGBUS
    SIGBUS,
#endif
    SIGFPE,  SIGILL, SIGABRT,
};

#define HOST_FAULT_SIGNAL_COUNT                                                \
    ((int)(sizeof(host_fault_signals) / sizeof(host_fault_signals[0])))

static struct sigaction               saved_actions[HOST_FAULT_SIGNAL_COUNT];
static int                            host_handler_depth;
static _Thread_local HostSignalGuard *active_guard;
static _Thread_local sig_atomic_t     handlers_suspended;
static _Thread_local const char *volatile native_call;

static int host_signal_index(int sig) {
    for (int i = 0; i < HOST_FAULT_SIGNAL_COUNT; i++)
        if (host_fault_signals[i] == sig)
            return i;
    return -1;
}

static void restore_and_raise_default(int sig, int idx) {
    sigaction(sig, &saved_actions[idx], NULL);
    raise(sig);
}

static void chain_saved_handler(int sig, siginfo_t *info, void *context) {
    int idx = host_signal_index(sig);
    if (idx < 0)
        return;

    struct sigaction *old = &saved_actions[idx];
    if (old->sa_handler == SIG_IGN)
        return;
    if (old->sa_handler == SIG_DFL) {
        restore_and_raise_default(sig, idx);
        return;
    }
    if (old->sa_flags & SA_SIGINFO)
        old->sa_sigaction(sig, info, context);
    else
        old->sa_handler(sig);
}

static void host_fault_handler(int sig, siginfo_t *info, void *context) {
    HostSignalGuard *guard = active_guard;
    if (!guard || handlers_suspended) {
        chain_saved_handler(sig, info, context);
        return;
    }

    SigSlot *slot = &guard->vm->vm_sigslots[sig];
    if (slot->action != 0) {
        _cccc_pending[sig] = 1;
        _cccc_any_pending  = 1;

        /* Asynchronous signals can return to the interrupted instruction and
         * will be delivered at the next dispatch. A synchronous hardware
         * fault cannot return safely, so unwind and resume at the VM's next
         * bytecode PC, where the guest disposition is applied. */
        if (!info || info->si_code <= 0)
            return;
        guard->guest_resume = 1;
    }

    guard->signal     = sig;
    guard->fault_addr = info ? info->si_addr : NULL;
    guard->native     = native_call;

    /* Print the host C backtrace while the faulting stack is still live —
     * after siglongjmp it will be unwound and unavailable.  Only do this on
     * the fatal path (not when the guest is going to resume). */
    if (!guard->guest_resume)
        cc_host_backtrace_print();

    siglongjmp(guard->env, 1);
}

void cccc_host_native_enter(const char *name) {
    native_call = name ? name : "?";
}

void cccc_host_native_leave(void) {
    native_call = NULL;
}

// A fault inside a native call can leave a libc lock held, so the diagnostic
// uses write() and _exit() only: stdio, malloc and error_tok could hang.
typedef struct {
    char   buf[512];
    size_t len;
} FaultLine;

static void fault_put(FaultLine *l, const char *s) {
    for (; *s && l->len < sizeof l->buf - 1; s++)
        l->buf[l->len++] = *s;
}

static void fault_put_int(FaultLine *l, long v) {
    char tmp[24];
    int  n = 0;
    if (v < 0) {
        fault_put(l, "-");
        v = -v;
    }
    do
        tmp[n++] = (char)('0' + v % 10);
    while ((v /= 10) > 0);
    while (n > 0 && l->len < sizeof l->buf - 1)
        l->buf[l->len++] = tmp[--n];
}

static void native_fault_exit(VirtualMachine *vm, int sig, const char *name) {
    FaultLine l   = {.len = 0};
    Token    *tok = vm->compiler.macro_call_tok;
    if (tok && tok->file && tok->file->name) {
        fault_put(&l, tok->file->name);
        fault_put(&l, ":");
        fault_put_int(&l, tok->line_no);
        fault_put(&l, ": ");
    }
    fault_put(&l, "error: compile-time execution terminated by host signal ");
    fault_put_int(&l, sig);
    fault_put(&l, " in '");
    fault_put(&l, name);
    fault_put(&l, "'\n");
    ssize_t ignored = write(STDERR_FILENO, l.buf, l.len);
    (void)ignored;
    _exit(1);
}

static bool host_signal_debug_active(VirtualMachine *vm) {
    return vm && (vm->flags & CCCC_ENABLE_DEBUGGER) &&
           !(vm->flags & CCCC_NO_DEBUG_ON_CRASH) &&
           !vm->compiler.testing_mode && isatty(fileno(stdin)) &&
           isatty(fileno(stdout));
}

static bool install_host_handlers(HostSignalGuard *guard) {
    guard->previous = active_guard;

    if (host_handler_depth == 0) {
        struct sigaction action;
        memset(&action, 0, sizeof(action));
        sigemptyset(&action.sa_mask);
        action.sa_sigaction = host_fault_handler;
        action.sa_flags     = SA_SIGINFO;

        int installed       = 0;
        for (int i = 0; i < HOST_FAULT_SIGNAL_COUNT; i++) {
            int sig = host_fault_signals[i];
            if (sigaction(sig, NULL, &saved_actions[i]) != 0 ||
                sigaction(sig, &action, NULL) != 0) {
                for (int j = 0; j < installed; j++)
                    sigaction(host_fault_signals[j], &saved_actions[j], NULL);
                return false;
            }
            installed++;
        }
    }

    host_handler_depth++;
    active_guard = guard;
    return true;
}

static void uninstall_host_handlers(HostSignalGuard *guard) {
    active_guard = guard->previous;
    if (host_handler_depth <= 0)
        return;
    host_handler_depth--;
    if (host_handler_depth == 0) {
        for (int i = 0; i < HOST_FAULT_SIGNAL_COUNT; i++)
            sigaction(host_fault_signals[i], &saved_actions[i], NULL);
    }
}

/* use_siginfo (#745): when action == 2 (VM handler) and the guest slot's
   sa_flags has SA_SIGINFO set, install the async-safe three-argument shim
   (_cccc_sig_shim_info) with SA_SIGINFO instead of the plain one-argument
   shim, so the real host siginfo_t is captured at delivery time.
   passthrough_flags (#787): SA_RESTART/SA_NOCLDSTOP/SA_NOCLDWAIT, passed
   through onto the real host sigaction() -- these are kernel-level
   concerns (restarting an interrupted blocking FFI syscall like read()/
   pause(), suppressing SIGCHLD delivery/queuing) that the VM dispatch loop
   cannot meaningfully emulate itself, unlike sa_mask/SA_NODEFER/
   SA_RESETHAND which are enforced in the VM (see cccc_signal_prepare_delivery
   in src/stdlib/signal.c). Not applied for action == 0 (SIG_DFL): the
   kernel's own default disposition already governs restart/notification
   behavior there. */
static void make_guest_action(struct sigaction *sa, int action,
                              bool use_siginfo, int passthrough_flags) {
    memset(sa, 0, sizeof(*sa));
    sigemptyset(&sa->sa_mask);
    if (action == 1) {
        sa->sa_handler = SIG_IGN;
        sa->sa_flags   = passthrough_flags;
    } else if (action == 2) {
        if (use_siginfo) {
            sa->sa_sigaction = _cccc_sig_shim_info;
            sa->sa_flags     = SA_SIGINFO | passthrough_flags;
        } else {
            sa->sa_handler = _cccc_sig_shim;
            sa->sa_flags   = passthrough_flags;
        }
    } else {
        sa->sa_handler = SIG_DFL;
    }
}

int cccc_set_guest_signal_action(VirtualMachine *vm, int sig, int action) {
    int  idx = host_signal_index(sig);
    bool use_siginfo =
        vm && action == 2 && (vm->vm_sigslots[sig].sa_flags & SA_SIGINFO) != 0;
    int passthrough = 0;
    if (vm && sig > 0 && sig < CCCC_NSIG)
        passthrough = vm->vm_sigslots[sig].sa_flags &
                      (SA_RESTART | SA_NOCLDSTOP | SA_NOCLDWAIT);
    struct sigaction desired;
    make_guest_action(&desired, action, use_siginfo, passthrough);

    if (idx >= 0 && host_handler_depth > 0) {
        sigset_t block, oldmask;
        sigemptyset(&block);
        sigaddset(&block, sig);
        sigprocmask(SIG_BLOCK, &block, &oldmask);
        saved_actions[idx] = desired;
        sigprocmask(SIG_SETMASK, &oldmask, NULL);
        return 0;
    }
    return sigaction(sig, &desired, NULL);
}

// A guest longjmp out of a callback lands here (cccc_eval_landing_jump): the
// vm_eval that made the native call resumes dispatching at the longjmp target.
typedef struct EvalLanding EvalLanding;
struct EvalLanding {
    jmp_buf          env;
    EvalLanding     *previous;
    HostSignalGuard *guard;
    const char      *native;
    VirtualMachine  *ffi_vm;
    CcSlotSave      *slots;
};

static _Thread_local EvalLanding *eval_landing;

void *cccc_eval_landing_current(void) {
    return eval_landing;
}

void cccc_eval_landing_set(void *landing) {
    eval_landing = landing;
}

// Undoes, while the abandoned frames are still live, what they would have
// undone on return, then continues in the vm_eval that owns `landing`.
_Noreturn void cccc_eval_landing_jump(void *landing) {
    EvalLanding *pad = landing;
    while (active_guard && active_guard != pad->guard)
        uninstall_host_handlers(active_guard);
    native_call = pad->native;
    cccc_ffi_vm_set(pad->ffi_vm);
    cccc_slot_saves_unwind_to(pad->slots);
    eval_landing = pad;
    _longjmp(pad->env, 1);
}

static int eval_with_landing(VirtualMachine *vm, volatile Pc *current_pc) {
    EvalLanding pad;
    pad.previous = eval_landing;
    pad.guard    = active_guard;
    pad.native   = native_call;
    pad.ffi_vm   = cccc_current_ffi_vm();
    pad.slots    = cccc_slot_saves;
    eval_landing = &pad;
    // A landing resumes dispatch at vm->pc, the longjmp target.
    _setjmp(pad.env);
    int rc       = cccc_vm_eval_dispatch(vm, current_pc);
    eval_landing = pad.previous;
    return rc;
}

int vm_eval(VirtualMachine *vm) {
    volatile Pc current_pc = vm ? vm->pc : CCCC_INVALID_PC;
    vm->cycle              = 0;

    bool debugger          = host_signal_debug_active(vm);
    if (!debugger && !vm->comptime_depth)
        return eval_with_landing(vm, &current_pc);

    // A callback re-entering from a native call starts with no native call of
    // its own active.
    const char *volatile outer_native = native_call;
    native_call                       = NULL;

    for (;;) {
        HostSignalGuard guard;
        memset(&guard, 0, sizeof(guard));
        guard.vm       = vm;
        guard.previous = active_guard;

        if (sigsetjmp(guard.env, 1) != 0) {
            uninstall_host_handlers(&guard);
            native_call = NULL;

            if (guard.guest_resume)
                continue;

            if (guard.native && !debugger)
                native_fault_exit(vm, guard.signal, guard.native);

            vm->dbg.host_fault_signal = guard.signal;
            vm->pc                    = current_pc;
            if (debugger) {
                handlers_suspended = 1;
                cc_debug_repl_host_fault(vm, guard.signal,
                                         (void *)guard.fault_addr);
                handlers_suspended = 0;
            }
            native_call = outer_native;
            return CCCC_HOST_SIGNAL_RC;
        }

        if (!install_host_handlers(&guard)) {
            native_call = outer_native;
            return eval_with_landing(vm, &current_pc);
        }

        int rc = eval_with_landing(vm, &current_pc);
        uninstall_host_handlers(&guard);
        native_call = outer_native;
        return rc;
    }
}

#else

void cccc_host_native_enter(const char *name) {
    (void)name;
}

void cccc_host_native_leave(void) {}

void *cccc_eval_landing_current(void) {
    return NULL;
}

void cccc_eval_landing_set(void *landing) {
    (void)landing;
}

_Noreturn void cccc_eval_landing_jump(void *landing) {
    (void)landing;
    abort();
}

int cccc_set_guest_signal_action(VirtualMachine *vm, int sig, int action) {
    (void)vm;
    void (*handler)(int) = action == 1   ? SIG_IGN
                           : action == 2 ? _cccc_sig_shim
                                         : SIG_DFL;
    return signal(sig, handler) == SIG_ERR ? -1 : 0;
}

int vm_eval(VirtualMachine *vm) {
    volatile Pc current_pc = vm ? vm->pc : CCCC_INVALID_PC;
    vm->cycle              = 0;
    return cccc_vm_eval_dispatch(vm, &current_pc);
}

#endif
