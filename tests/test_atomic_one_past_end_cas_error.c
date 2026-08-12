// EXPECT_RUNTIME_ERROR CCCC_FLAGS: -2
// #985: ACAS mirror of test_atomic_one_past_end_load_error.c, checking the
// object pointer (REG_A0). Same #497 reasoning as
// test_atomic_one_past_end_exchange_error.c -- ACAS's operand word carries
// the same hazard, and this CHKD is emitted ahead of ACAS as a standalone
// instruction that never touches it.
#include <stdatomic.h>
#include <stdbool.h>
#include <stdlib.h>
int main(void) {
    _Atomic int *p = malloc(4 * sizeof(int));   // valid indices 0..3
    if (!p)
        return 255;
    _Atomic int *q = p + 4;   // exactly one past the end -- legal to form
    int expected = 0;
    // dereferencing the object pointer -- must trap
    bool ok = atomic_compare_exchange_strong(q, &expected, 1);
    free((void *)p);
    return ok ? 1 : 2;
}
