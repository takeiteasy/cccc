// signal.h stdlib — VM-managed signal handling
#include "../jcc.h"
#include <signal.h>

/* Global async-safe pending flags (written only by native signal shims) */
volatile sig_atomic_t _jcc_pending[JCC_NSIG];
volatile sig_atomic_t _jcc_any_pending;

/* Minimal async-safe shim: only sets pending flags, never touches VM state */
void _jcc_sig_shim(int sig) {
    if (sig > 0 && sig < JCC_NSIG) {
        _jcc_pending[sig] = 1;
        _jcc_any_pending  = 1;
    }
}

void register_signal_functions(JCC *vm) {
    /* signal() and raise() are handled via VSIGNAL/VRAISE opcodes;
       no FFI registration is needed */
    (void)vm;
}
