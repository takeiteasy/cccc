// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers -O3
// CCCC_MATRIX_SKIP: depends on -O3 specifically; the matrix only ever runs -O0
// + one -f pass. See test_checked_pointers_prop_opt_fusion_o2.c -- same proof
// at -O3.

int main(void) {
    int n                                  = 4;
    int *[[cccc::array, cccc::count(n)]] p = (int[4]){1, 2, 3, 4};
    int          other[16]                 = {0};
    volatile int c                         = 1;
    int         *q                         = other;
    if (c)
        q = p;
    volatile int i = 10;
    int          x = q[i];
    return x;
}
