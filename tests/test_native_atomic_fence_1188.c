// atomic fences round-trip under -c=native.
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
