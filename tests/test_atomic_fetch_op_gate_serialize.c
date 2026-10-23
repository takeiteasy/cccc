// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: __atomic_fetch_add\(.*&i, 1, __ATOMIC_RELAXED\)
// CCCC_EXPECT_STDOUT: __atomic_add_fetch\(.*&i, 1, __ATOMIC_SEQ_CST\)
// CCCC_EXPECT_STDOUT: __atomic_fetch_nand\(.*&i, 1, __ATOMIC_SEQ_CST\)
// CCCC_EXPECT_STDOUT: __atomic_fetch_sub\(.*&q, .*1, __ATOMIC_SEQ_CST\)
// CCCC_EXPECT_STDOUT: __atomic_compare_exchange_n\(
// CCCC_REJECT_STDOUT: __atomic_fetch_(or|and)\(.*&(b|q), 1
//
// Only operands every host compiler accepts become one builtin: a _Bool, or
// and/or/xor on a pointer, keeps the compare-exchange loop, and a GCC pointer
// operation moves bytes.
int   i;
_Bool b;
int  *q;

int main(void) {
    __atomic_fetch_add(&i, 1, __ATOMIC_RELAXED);
    __atomic_add_fetch(&i, 1, __ATOMIC_SEQ_CST);
    __atomic_fetch_nand(&i, 1, __ATOMIC_SEQ_CST);
    __atomic_fetch_sub(&q, 1, __ATOMIC_SEQ_CST);
    __atomic_fetch_or(&b, 1, __ATOMIC_SEQ_CST);
    __atomic_fetch_and(&q, 1, __ATOMIC_SEQ_CST);
    return 42;
}
