// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers -O3
// CCCC_MATRIX_SKIP: depends on -O3 specifically; the matrix only ever runs -O0
// + one -f pass. See test_checked_pointers_prop_chain_fusion_o2.c -- same proof
// at -O3.

int main(void) {
    int n                                  = 4;
    int *[[cccc::array, cccc::count(n)]] p = (int[4]){1, 2, 3, 4};
    int         *q                         = p + 0;
    int         *r                         = q + 1;
    int         *s                         = r + 1;
    volatile int i                         = 2;
    int          x                         = s[i];
    return x;
}
