// CCCC_FLAGS: -fopenmp -m
// CCCC_EXPECT_STDOUT: __atomic_load_n\(\(unsigned int \*\)&r, __ATOMIC_ACQUIRE\)
// CCCC_EXPECT_STDOUT: __atomic_store_n\(\(unsigned int \*\)&w, [a-z_0-9]+, __ATOMIC_RELEASE\)
// CCCC_EXPECT_STDOUT: __atomic_store_n\(\(unsigned int \*\)&w, [a-z_0-9]+, __ATOMIC_RELAXED\)
// CCCC_EXPECT_STDOUT: , 0, __ATOMIC_RELAXED, __ATOMIC_RELAXED\)\);
// CCCC_EXPECT_STDOUT: , 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE\)\);
// CCCC_EXPECT_STDOUT: , 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST\)\);
// CCCC_EXPECT_STDOUT: __db, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE\)
// CCCC_EXPECT_STDOUT: __db[a-z_0-9]*, 1, __ATOMIC_RELAXED, __ATOMIC_RELAXED\)
//
// The memory-order clauses of `omp atomic` reach the emitted __atomic builtins;
// with no clause an atomic is relaxed, `read acq_rel` acquires and
// `write acq_rel` releases.

int r, w, u, e, d = 1;

int main(void) {
#pragma omp parallel
    {
        int v;
#pragma omp atomic read acquire
        v = r;
#pragma omp atomic write release
        w = v;
#pragma omp atomic write
        w = v;
#pragma omp atomic
        u += 1;
#pragma omp atomic acq_rel
        u += 1;
#pragma omp atomic seq_cst
        u += 1;
#pragma omp atomic compare release fail(acquire)
        if (e == 0) { e = d; }
#pragma omp atomic compare weak
        if (e == 1) { e = 2; }
    }
    return 42;
}
