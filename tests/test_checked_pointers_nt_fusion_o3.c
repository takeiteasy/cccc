// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers -O3
// CCCC_MATRIX_SKIP: depends on -O3 specifically; the matrix only ever runs -O0 + one -f pass.
// Same proof as test_checked_pointers_nt_fusion_o2.c, one level higher.

int main(void) {
    int n = 3;
    char * [[cccc::ntarray, cccc::count(n)]] s = (char[4]){'a', 'b', 'c', 0};
    volatile int i = 3; // the terminator slot
    s[i] = 'x'; // non-null -- must still trap under fusion
    return 0;
}
