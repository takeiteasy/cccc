// EXPECT_RUNTIME_ERROR CCCC_FLAGS: -3 --optimize=3
// CCCC_MATRIX_SKIP: depends on --optimize=3 (tail-call elimination)
// Ticket #756: a UAF safety check was bypassed (host SIGSEGV in op_CALL_fn
// instead of a controlled TEMPORAL SAFETY VIOLATION trap) when a nested call
// preceded free() inside a tail-called function, under -3 --optimize=3.
// Root cause: op_CALLT_fn (src/ops.c) incremented vm->shadow_sp under CFI
// even though it deliberately leaves the CALL's return address on the main
// stack for the tail-callee's eventual LEV3 to consume -- desynchronising
// the shadow stack by one entry per tail call. Requires all three
// ingredients together: (1) touch(p) called in tail position from main,
// (2) a nested call (g(*p)) inside touch's body before the UAF deref, and
// (3) --optimize=3 (CALLT eligibility).
#include <stdlib.h>

static int g(int v) { return v; }

static int touch(int *p) {
    int a = g(*p);
    free((void *)p);
    int b = *p; // must be caught as a controlled UAF, not a host crash
    return a + b;
}

int main(void) {
    int *p = malloc(sizeof(int));
    *p = 5;
    return touch(p); // tail call: main -> touch
}
