// EXPECT_RUNTIME_ERROR CCCC_FLAGS: -2
// #985: ALDR bypasses emit_load_ex/emit_load_safety_checks entirely (it's
// codegen'd directly from ND_ALOAD), so it needed its own CHKD emission --
// otherwise an atomic_load through a one-past-the-end pointer (legal to
// FORM since #983, illegal to dereference) would silently escape bounds
// checking, unlike a plain scalar load of the same address.
#include <stdatomic.h>
#include <stdlib.h>
int main(void) {
    _Atomic int *p = malloc(4 * sizeof(int));   // valid indices 0..3
    if (!p)
        return 255;
    _Atomic int *q = p + 4;   // exactly one past the end -- legal to form
    int v = atomic_load(q);   // dereferencing it -- must trap
    free((void *)p);
    return v;
}
