// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers -O2
// CCCC_MATRIX_SKIP: depends on -O2 specifically; the matrix only ever runs -O0
// + one -f pass. #923's CHKNT (null-terminator guard for [[cccc::ntarray]]) is
// a value-dependent check, emitted from the store path in codegen.c's ND_ASSIGN
// case rather than gen_addr -- more exposed to fusion than CHKR was, since a
// fused indexed store is exactly the s[n] = c shape this test exercises. Proves
// a non-null write into the widened terminator slot still traps at -O2 rather
// than silently corrupting the invariant.

int main(void) {
    int n                                     = 3;
    char *[[cccc::ntarray, cccc::count(n)]] s = (char[4]){'a', 'b', 'c', 0};
    volatile int i                            = 3; // the terminator slot
    s[i] = 'x'; // non-null -- must still trap under fusion
    return 0;
}
