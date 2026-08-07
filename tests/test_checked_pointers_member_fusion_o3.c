// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers -O3
// CCCC_MATRIX_SKIP: depends on -O3 specifically; the matrix only ever runs -O0 + one -f pass.
// See test_checked_pointers_member_fusion_o2.c -- same proof at -O3.

struct S {
    int n;
    int * [[cccc::array, cccc::count(n)]] p;
};

int main(void) {
    struct S s = {4, (int[4]){1, 2, 3, 4}};
    volatile int i = 4;
    int x = s.p[i];
    return x;
}
