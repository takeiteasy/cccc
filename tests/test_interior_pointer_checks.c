// CCCC_FLAGS: --testing
// Ticket #650: CHKB and CHKP3 resolve interior heap pointers (p + k) via
// vm->sorted_allocs (the same range-query table #647 added for
// __builtin_dynamic_object_size), not just exact base pointers.
//
// These are the *positive* cases: valid accesses through an interior
// pointer, including a negative index that lands back within the
// allocation, must not raise a bounds error. This locks in the fix to
// CHKB's negative-offset check, which used to reject any negative scaled
// offset before resolving the allocation — a false positive for interior
// base pointers with an in-bounds negative index.

#include <stdlib.h>

[[cccc::test(flags = "-V --bounds-checks")]]
void test_interior_forward_index_in_bounds(void) {
    int *q = malloc(4 * sizeof(int));
    q[0]   = 10;
    q[1]   = 20;
    q[2]   = 30;
    q[3]   = 40;
    int *p = q + 2;     // interior pointer, offset 2 ints into the allocation
    AssertEq(p[1], 40); // effective offset 2+1=3, within the 4-int allocation
    free(q);
}

[[cccc::test(flags = "-V --bounds-checks")]]
void test_interior_negative_index_in_bounds(void) {
    int *q = malloc(4 * sizeof(int));
    q[0]   = 10;
    q[1]   = 20;
    q[2]   = 30;
    q[3]   = 40;
    int *p = q + 2;      // interior pointer, offset 2 ints into the allocation
    AssertEq(p[-1], 20); // effective offset 2-1=1: valid, within the allocation
    free(q);
}
