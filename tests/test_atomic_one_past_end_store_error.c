// EXPECT_RUNTIME_ERROR CCCC_FLAGS: -2
// #985: ASTR mirror of test_atomic_one_past_end_load_error.c -- ASTR
// bypasses emit_store_ex entirely (codegen'd directly from ND_ASTORE), so
// it needed its own CHKD too. atomic_store through a one-past-the-end
// pointer must still trap. Note: pre-fix this write happened to also trip
// the (incidental, allocation-size-dependent) heap canary a few bytes
// further out, so this particular test isn't itself proof CHKD fired --
// see the "ARRAY BOUNDS ERROR" vs. "HEAP CANARY CORRUPTED" banner text to
// tell which check actually caught it.
#include <stdatomic.h>
#include <stdlib.h>
int main(void) {
    _Atomic int *p = malloc(4 * sizeof(int)); // valid indices 0..3
    if (!p)
        return 255;
    _Atomic int *q = p + 4; // exactly one past the end -- legal to form
    atomic_store(q, 1);     // dereferencing it -- must trap
    free((void *)p);
    return 42;
}
