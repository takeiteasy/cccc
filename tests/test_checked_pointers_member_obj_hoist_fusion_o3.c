// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers -O3
// CCCC_MATRIX_SKIP: depends on -O3 specifically; the matrix only ever runs -O0
// + one -f pass. See test_checked_pointers_member_obj_hoist_fusion_o2.c -- same
// proof at -O3.

struct S {
    int n;
    int *[[cccc::array, cccc::count(n)]] p;
};

int main(void) {
    struct S arr[2] = {
        {4, (int[4]){1, 2, 3, 4}},
        {4, (int[4]){5, 6, 7, 8}},
    };
    volatile int k = 1;
    volatile int i = 4;
    int          x = arr[k].p[i];
    return x;
}
