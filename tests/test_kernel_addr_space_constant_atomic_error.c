// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: writing to constant memory
#include <stdatomic.h>
[[cccc::kernel]] static void k([[cccc::constant]] _Atomic int *c) {
    atomic_fetch_add_explicit(c, 1, memory_order_relaxed);
}
int main(void) {
    return 42;
}
