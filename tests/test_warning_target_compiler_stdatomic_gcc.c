// CCCC_FLAGS: -E --output-compiler=gcc
// CCCC_REJECT_STDERR: (warning|error):
// CCCC_REJECT_STDOUT: __c11_atomic
// CCCC_REJECT_STDOUT: __builtin_atomic_
// CCCC_EXPECT_STDOUT: __atomic_thread_fence
// CCCC_EXPECT_STDOUT: __atomic_fetch_add
#include <stdatomic.h>
int *_Atomic p;
int main(void) {
    atomic_thread_fence(memory_order_seq_cst);
    atomic_fetch_add(&p, 1);
    return 42;
}
