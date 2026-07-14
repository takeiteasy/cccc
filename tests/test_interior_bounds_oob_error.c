// EXPECT_RUNTIME_ERROR CCCC_FLAGS: -V --bounds-checks
// Ticket #650: CHKB now resolves interior heap pointers (p + k) via
// vm->sorted_allocs, not just exact base pointers. An out-of-bounds index
// applied through an interior pointer must be caught, not silently missed.
#include <stdlib.h>

int main(void) {
    int *q = malloc(4 * sizeof(int));
    int *p = q + 2; // interior pointer, offset 2 ints into the allocation
    return p[5];    // offset 2+5=7 ints >= 4 allocated: out of bounds
}
