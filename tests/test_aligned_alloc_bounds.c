// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --bounds-checks
// Ticket #668: before this fix, aligned_alloc() bypassed the VM heap
// entirely and allocated via the host allocator through FFI, so its memory
// carried no AllocHeader and CHKB's bounds check silently never applied to
// it. Now that aligned_alloc is routed through MALCA (the same
// alignment-aware bump allocator malloc uses), an out-of-bounds write past
// an aligned_alloc'd buffer must be caught just like it would for malloc.
#include <stdlib.h>

int main(void) {
    int *p = aligned_alloc(32, 4 * sizeof(int));
    p[10] = 1; // 10 >= 4 allocated ints: out of bounds
    return 42;
}
