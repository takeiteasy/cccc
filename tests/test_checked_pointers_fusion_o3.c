// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers -O3
// CCCC_MATRIX_SKIP: depends on -O3 specifically; the matrix only ever runs -O0
// + one -f pass. Same proof as test_checked_pointers_fusion_o2.c, one level
// higher.

int main(void) {
    int n                                  = 4;
    int *[[cccc::array, cccc::count(n)]] a = (int[4]){1, 2, 3, 4};
    volatile int i                         = 4;
    int          x                         = a[i];
    return x;
}
