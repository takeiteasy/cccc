// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers -O2
// CCCC_MATRIX_SKIP: depends on -O2 specifically; the matrix only ever runs -O0 + one -f pass.
// #943's CHKNT propagation is a value-dependent check emitted from the store
// path, same exposure to fusion as the direct-access case
// (test_checked_pointers_nt_fusion_o2.c). Proves a non-null write into the
// propagated terminator slot still traps at -O2.

int main(void) {
    int n = 3;
    char * [[cccc::ntarray, cccc::count(n)]] s = (char[4]){'a', 'b', 'c', 0};
    char *q = s;
    volatile int i = 3; // the terminator slot
    q[i] = 'x'; // non-null -- must still trap under fusion
    return 0;
}
