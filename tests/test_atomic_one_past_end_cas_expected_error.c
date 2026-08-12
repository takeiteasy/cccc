// EXPECT_RUNTIME_ERROR CCCC_FLAGS: -2
// #985: the load-bearing ACAS test -- the OBJECT pointer is in bounds, but
// the EXPECTED pointer is exactly one past the end of its own allocation.
// ACAS reads (and, on failure, writes) through `expected`, so it needs its
// own CHKD on REG_A1 independent of the REG_A0 check that
// test_atomic_one_past_end_cas_error.c already proves -- a fix that only
// guarded the object pointer would pass that test while still leaving this
// dereference unchecked. Note: pre-fix this happened to also trip the
// (incidental, allocation-size-dependent) heap canary a few bytes further
// out -- see the "ARRAY BOUNDS ERROR" vs. "HEAP CANARY CORRUPTED" banner
// text to tell which check actually caught it.
#include <stdatomic.h>
#include <stdbool.h>
#include <stdlib.h>
int main(void) {
    _Atomic int *obj = malloc(sizeof(int));
    int *backing = malloc(4 * sizeof(int));   // valid indices 0..3
    if (!obj || !backing)
        return 255;
    *obj = 0;
    int *expected = backing + 4;   // exactly one past the end -- legal to form
    // dereferencing `expected` -- must trap
    bool ok = atomic_compare_exchange_strong(obj, expected, 1);
    free((void *)obj);
    free(backing);
    return ok ? 1 : 2;
}
