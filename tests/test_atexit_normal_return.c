// #738 regression: atexit() handlers used to be a raw passthrough to the
// real host atexit(), which crashed the moment the host's real libc exit
// sequence invoked a guest function-pointer value as machine code. Handlers
// are now VM-owned and drained explicitly. ISO C requires them to run on
// normal return from main() (not just an explicit exit() call) -- verified
// here via cc_run_atexit_entries (vm.c), the top-level cc_run_at-per-handler
// drain used specifically because the GIL is already released by the time
// main() has returned (see the comment on cccc_call_guest_callback in
// internal.h for why that context can't use the nested-reentry path
// wrap_exit uses instead).
#include <stdlib.h>
#include <unistd.h>

static int order[3];
static int order_idx;

static void h2(void) {
    order[order_idx++] = 2;
}
static void h3(void) {
    order[order_idx++] = 3;
}

// Registered first, so LIFO means it must run LAST -- once h2/h3 have
// already recorded their positions, so this is the one that can verify the
// full order and calls _exit() directly: this file's own exit code is the
// only observable signal that this handler (and therefore the whole
// atexit-drain-on-normal-return path) actually ran.
static void h1_verify_and_exit(void) {
    order[order_idx++] = 1;
    if (order[0] == 3 && order[1] == 2 && order[2] == 1)
        _exit(42);
    _exit(1);
}

int main(void) {
    atexit(h1_verify_and_exit);
    atexit(h2);
    atexit(h3);
    return 0; // normal return from main -- must trigger the atexit drain
}
