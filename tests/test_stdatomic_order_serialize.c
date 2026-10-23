// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: __atomic_load_n\(.*&x, __ATOMIC_ACQUIRE\)
// CCCC_EXPECT_STDOUT: __atomic_store_n\(.*&x, 1, __ATOMIC_RELEASE\)
// CCCC_EXPECT_STDOUT: __atomic_exchange_n\(.*&x, 2, __ATOMIC_ACQ_REL\)
// CCCC_EXPECT_STDOUT: &e, 3, 1, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE\)
// CCCC_EXPECT_STDOUT: &e, 3, 0, __ATOMIC_RELAXED, __ATOMIC_RELAXED\)
// CCCC_EXPECT_STDOUT: , 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST\)
// CCCC_EXPECT_STDOUT: __atomic_fetch_add\(.*&x, 1, __ATOMIC_RELAXED\)
// CCCC_EXPECT_STDOUT: __atomic_fetch_sub\(.*&x, 2, __ATOMIC_ACQ_REL\)
// CCCC_EXPECT_STDOUT: __atomic_fetch_xor\(.*&x, 3, __ATOMIC_SEQ_CST\)
// CCCC_EXPECT_STDOUT: __atomic_fetch_add\(.*&p, .* 4LL, __ATOMIC_SEQ_CST\)
// CCCC_EXPECT_STDOUT: __atomic_exchange_n\(.*&flag, 1, __ATOMIC_ACQUIRE\)
// CCCC_EXPECT_STDOUT: __atomic_store_n\(.*&flag, 0, __ATOMIC_RELEASE\)
// CCCC_REJECT_STDOUT: __atomic_fetch_(and|or)
//
// The stdatomic.h _explicit forms reach the host builtins with their constant
// order and weak flag; a fetch-and-op is one builtin, and a pointer operand
// scales by its pointee. An unordered form is seq_cst.
#include <stdatomic.h>

atomic_int  x;
atomic_flag flag;
int         a[4];
_Atomic(int *) p;

int main(void) {
    int e = 0;
    atomic_load_explicit(&x, memory_order_acquire);
    atomic_store_explicit(&x, 1, memory_order_release);
    atomic_exchange_explicit(&x, 2, memory_order_acq_rel);
    atomic_compare_exchange_weak_explicit(&x, &e, 3, memory_order_acq_rel,
                                          memory_order_acquire);
    atomic_compare_exchange_strong_explicit(&x, &e, 3, memory_order_relaxed,
                                            memory_order_relaxed);
    atomic_compare_exchange_strong(&x, &e, 3);
    atomic_fetch_add_explicit(&x, 1, memory_order_relaxed);
    atomic_fetch_sub_explicit(&x, 2, memory_order_acq_rel);
    atomic_fetch_xor(&x, 3);
    atomic_fetch_add(&p, 1);
    atomic_flag_test_and_set_explicit(&flag, memory_order_acquire);
    atomic_flag_clear_explicit(&flag, memory_order_release);
    return 42;
}
