// CCCC_FLAGS: -2
// #985: positive control for the new atomic-op CHKD emissions -- an
// in-bounds atomic access on the LAST valid element (p+3 of a 4-int
// allocation), not p+0. p+0 would pass even if the access-size immediate
// were mis-encoded too large (e.g. 8 instead of 4); p+3 makes
// `base_off=12 + access_size=4 > size=16` false only when the size is
// correct, so this is the test that would catch an over-large CHKD size
// immediate, not just an off-by-one in the address.
#include <stdatomic.h>
#include <stdbool.h>
#include <stdlib.h>
int main(void) {
    _Atomic int *p = malloc(4 * sizeof(int)); // valid indices 0..3
    if (!p)
        return 255;
    _Atomic int *last = p + 3;                // last valid element -- in bounds

    atomic_store(last, 10);
    if (atomic_load(last) != 10) {
        free((void *)p);
        return 1;
    }

    int old = atomic_exchange(last, 20);
    if (old != 10) {
        free((void *)p);
        return 2;
    }

    int  expected = 20;
    bool ok       = atomic_compare_exchange_strong(last, &expected, 30);
    if (!ok || atomic_load(last) != 30) {
        free((void *)p);
        return 3;
    }

    free((void *)p);
    return 42;
}
