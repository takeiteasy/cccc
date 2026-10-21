// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: __atomic_thread_fence\(__ATOMIC_SEQ_CST\)
// CCCC_EXPECT_STDOUT: __atomic_signal_fence\(order\)
// CCCC_REJECT_STDOUT: [^_]atomic_thread_fence\(
// -m output keeps the fence builtins and no atomic_thread_fence call.
#include <stdatomic.h>
int main(void) {
    _Atomic int x = 0;
    atomic_thread_fence(memory_order_seq_cst);
    int order = memory_order_acquire;
    atomic_signal_fence(order);
    atomic_store(&x, 7);
    if (atomic_load(&x) != 7)
        return 1;
    return 42;
}
