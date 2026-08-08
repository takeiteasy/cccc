// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers -O3
// CCCC_MATRIX_SKIP: depends on -O3 specifically; the matrix only ever runs -O0 + one -f pass.
// Same as test_checked_pointers_prop_nt_fusion_o2.c, one level up -- proves
// #943's propagated CHKNT survives -O3's additional fusion/DCE/copy-prop
// passes too.

int main(void) {
    int n = 3;
    char * [[cccc::ntarray, cccc::count(n)]] s = (char[4]){'a', 'b', 'c', 0};
    char *q = s;
    volatile int i = 3; // the terminator slot
    q[i] = 'x'; // non-null -- must still trap under fusion
    return 0;
}
