// signal.h stdlib — VM-managed signal handling
#include "../cccc.h"
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

void register_signal_functions(VirtualMachine *vm) {
    /* signal() and raise() are handled via VSIGNAL/VRAISE opcodes;
       no FFI registration is needed */
    cc_register_cfunc(vm, "sigaction",   (void*)sigaction,   3, 0);
    cc_register_cfunc(vm, "sigemptyset", (void*)sigemptyset, 1, 0);
    cc_register_cfunc(vm, "sigfillset",  (void*)sigfillset,  1, 0);
    cc_register_cfunc(vm, "sigaddset",   (void*)sigaddset,   2, 0);
    cc_register_cfunc(vm, "sigdelset",   (void*)sigdelset,   2, 0);
    cc_register_cfunc(vm, "sigismember", (void*)sigismember, 2, 0);
}
