// EXPECT_RUNTIME_ERROR CCCC_FLAGS: -V --uaf-detection
// Ticket #650: CHKP3 now resolves interior heap pointers (p + k) via
// vm->sorted_allocs, not just exact base pointers. A use-after-free through
// an interior pointer must be caught, not silently missed.
#include <stdlib.h>

int main(void) {
    int *q = malloc(4 * sizeof(int));
    int *p = q + 2; // interior pointer into the allocation
    free(q);
    return *p; // use-after-free via an interior pointer
}
