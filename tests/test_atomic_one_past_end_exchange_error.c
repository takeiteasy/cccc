// EXPECT_RUNTIME_ERROR CCCC_FLAGS: -2
// #985: AXCHG mirror of test_atomic_one_past_end_load_error.c. AXCHG's
// operand word already carries the #497 register-aliasing hazard, which is
// exactly why this CHKD emission was originally deferred (#983) -- this
// test is the load-bearing proof that the standalone CHKD instruction
// emitted ahead of AXCHG still catches the out-of-bounds dereference
// without AXCHG's own operand decode getting disturbed. Note: pre-fix
// this write happened to also trip the (incidental, allocation-size-
// dependent) heap canary a few bytes further out -- see the
// "ARRAY BOUNDS ERROR" vs. "HEAP CANARY CORRUPTED" banner text to tell
// which check actually caught it; test_atomic_one_past_end_load_error.c
// and _cas_error.c are the two sites with no such coincidental backstop.
#include <stdatomic.h>
#include <stdlib.h>
int main(void) {
    _Atomic int *p = malloc(4 * sizeof(int));   // valid indices 0..3
    if (!p)
        return 255;
    _Atomic int *q = p + 4;        // exactly one past the end -- legal to form
    int old = atomic_exchange(q, 1); // dereferencing it -- must trap
    free((void *)p);
    return old;
}
