// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: __atomic_load_n\(&x, __ATOMIC_ACQUIRE\)
// CCCC_EXPECT_STDOUT: __atomic_load_n\(&x, __ATOMIC_RELAXED\)
// CCCC_EXPECT_STDOUT: __atomic_store_n\(&x, 1, __ATOMIC_RELEASE\)
// CCCC_EXPECT_STDOUT: __atomic_exchange_n\(&x, 2, __ATOMIC_ACQ_REL\)
// CCCC_EXPECT_STDOUT: &e, 3, 1, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE\)
// CCCC_EXPECT_STDOUT: &e, 3, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST\)
// CCCC_EXPECT_STDOUT: &e, 3, 0, __ATOMIC_RELAXED, __ATOMIC_RELAXED\)
// CCCC_EXPECT_STDOUT: __atomic_add_fetch\(.*&x, 1, __ATOMIC_ACQ_REL\)
//
// A constant order on an __atomic_* builtin is emitted; one the builtin would
// reject is raised to seq_cst (a release load, an acquire store, a release
// failure order), a failure order stronger than the success order raises it,
// and a computed order is seq_cst.

int x, y, o;

int main(void) {
    int e = 0;
    y     = __atomic_load_n(&x, __ATOMIC_ACQUIRE);
    y     = __atomic_load_n(&x, __ATOMIC_RELAXED);
    y     = __atomic_load_n(&x, __ATOMIC_RELEASE);
    __atomic_store_n(&x, 1, __ATOMIC_RELEASE);
    __atomic_store_n(&x, 1, __ATOMIC_ACQUIRE);
    y = __atomic_exchange_n(&x, 2, __ATOMIC_ACQ_REL);
    __atomic_compare_exchange_n(&x, &e, 3, 1, __ATOMIC_RELEASE,
                                __ATOMIC_ACQUIRE);
    __atomic_compare_exchange_n(&x, &e, 3, 0, __ATOMIC_RELAXED,
                                __ATOMIC_RELEASE);
    __atomic_compare_exchange_n(&x, &e, 3, 0, __ATOMIC_RELAXED,
                                __ATOMIC_RELAXED);
    __atomic_compare_exchange_n(&x, &e, 3, 0, o, o);
    y = __atomic_add_fetch(&x, 1, __ATOMIC_ACQ_REL);
    return 42;
}
