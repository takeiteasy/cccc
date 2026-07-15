// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --bounds-checks
// Ticket #668: posix_memalign is a distinct opcode/codegen path from
// aligned_alloc (PMEMA vs MALCA), so it needs its own proof that a heap
// safety feature actually fires on its allocations, not just that it runs
// without crashing. Before this fix, posix_memalign bypassed the VM heap
// via FFI and carried no AllocHeader, so CHKB never applied to it.
#include <stdlib.h>

int main(void) {
    int *p;
    if (posix_memalign((void **)&p, 32, 4 * sizeof(int)) != 0)
        return 1;
    p[10] = 1; // 10 >= 4 allocated ints: out of bounds
    return 42;
}
