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
    sigjmp_buf env;
    VirtualMachine *vm;
    HostSignalGuard *previous;
    volatile sig_atomic_t signal;
    volatile sig_atomic_t guest_resume;
    void *volatile fault_addr;
};

static const int host_fault_signals[] = {
    SIGSEGV,
#ifdef SIGBUS
    SIGBUS,
#endif
    SIGFPE,
    SIGILL,
    SIGABRT,
};

#define HOST_FAULT_SIGNAL_COUNT \
    ((int)(sizeof(host_fault_signals) / sizeof(host_fault_signals[0])))

static struct sigaction saved_actions[HOST_FAULT_SIGNAL_COUNT];
static int host_handler_depth;
static _Thread_local HostSignalGuard *active_guard;
static _Thread_local sig_atomic_t handlers_suspended;

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
        _cccc_any_pending = 1;

        /* Asynchronous signals can return to the interrupted instruction and
         * will be delivered at the next dispatch. A synchronous hardware
         * fault cannot return safely, so unwind and resume at the VM's next
         * bytecode PC, where the guest disposition is applied. */
        if (!info || info->si_code <= 0)
            return;
        guard->guest_resume = 1;
    }

    guard->signal = sig;
    guard->fault_addr = info ? info->si_addr : NULL;

    /* Print the host C backtrace while the faulting stack is still live —
     * after siglongjmp it will be unwound and unavailable.  Only do this on
     * the fatal path (not when the guest is going to resume). */
    if (!guard->guest_resume)
        cc_host_backtrace_print();

    siglongjmp(guard->env, 1);
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
        action.sa_flags = SA_SIGINFO;

        int installed = 0;
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
   shim, so the real host siginfo_t is captured at delivery time. */
static void make_guest_action(struct sigaction *sa, int action, bool use_siginfo) {
    memset(sa, 0, sizeof(*sa));
    sigemptyset(&sa->sa_mask);
    if (action == 1) {
        sa->sa_handler = SIG_IGN;
    } else if (action == 2) {
        if (use_siginfo) {
            sa->sa_sigaction = _cccc_sig_shim_info;
            sa->sa_flags = SA_SIGINFO;
        } else {
            sa->sa_handler = _cccc_sig_shim;
        }
    } else {
        sa->sa_handler = SIG_DFL;
    }
}

int cccc_set_guest_signal_action(VirtualMachine *vm, int sig, int action) {
    int idx = host_signal_index(sig);
    bool use_siginfo = vm && action == 2 &&
                        (vm->vm_sigslots[sig].sa_flags & SA_SIGINFO) != 0;
    struct sigaction desired;
    make_guest_action(&desired, action, use_siginfo);

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

int vm_eval(VirtualMachine *vm) {
    volatile Pc current_pc = vm ? vm->pc : CCCC_INVALID_PC;
    vm->cycle = 0;

    if (!host_signal_debug_active(vm))
        return cccc_vm_eval_dispatch(vm, &current_pc);

    for (;;) {
        HostSignalGuard guard;
        memset(&guard, 0, sizeof(guard));
        guard.vm = vm;
        guard.previous = active_guard;

        if (sigsetjmp(guard.env, 1) != 0) {
            uninstall_host_handlers(&guard);

            if (guard.guest_resume)
                continue;

            vm->dbg.host_fault_signal = guard.signal;
            vm->pc = current_pc;
            handlers_suspended = 1;
            cc_debug_repl_host_fault(vm, guard.signal, (void *)guard.fault_addr);
            handlers_suspended = 0;
            return CCCC_HOST_SIGNAL_RC;
        }

        if (!install_host_handlers(&guard))
            return cccc_vm_eval_dispatch(vm, &current_pc);

        int rc = cccc_vm_eval_dispatch(vm, &current_pc);
        uninstall_host_handlers(&guard);
        return rc;
    }
}

#else

int cccc_set_guest_signal_action(VirtualMachine *vm, int sig, int action) {
    (void)vm;
    void (*handler)(int) =
        action == 1 ? SIG_IGN : action == 2 ? _cccc_sig_shim : SIG_DFL;
    return signal(sig, handler) == SIG_ERR ? -1 : 0;
}

int vm_eval(VirtualMachine *vm) {
    volatile Pc current_pc = vm ? vm->pc : CCCC_INVALID_PC;
    vm->cycle = 0;
    return cccc_vm_eval_dispatch(vm, &current_pc);
}

#endif
