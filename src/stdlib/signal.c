// signal.h stdlib function registration
#include "../jcc.h"
#include <signal.h>

static long long wrap_raise(long long sig) {
    return (long long)raise((int)sig);
}

void register_signal_functions(JCC *vm) {
    cc_register_cfunc(vm, "signal", (void*)signal, 2, 0);
    cc_register_cfunc(vm, "raise", (void*)wrap_raise, 1, 0);
}
