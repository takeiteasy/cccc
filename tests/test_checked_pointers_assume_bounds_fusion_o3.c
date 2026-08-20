// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers -O3
// CCCC_MATRIX_SKIP: depends on -O3 specifically; the matrix only ever runs -O0
// + one -f pass. Same as test_checked_pointers_assume_bounds_fusion_o2.c, one
// level up -- proves #944's CHKAB survives -O3's additional
// fusion/DCE/copy-prop passes.

int main(void) {
    int *[[cccc::array, cccc::count(4)]] p = (int[4]){1, 2, 3, 4};
    int *[[cccc::array, cccc::count(10)]] q;
    q              = p;
    volatile int i = 7;
    return q[i];
}
