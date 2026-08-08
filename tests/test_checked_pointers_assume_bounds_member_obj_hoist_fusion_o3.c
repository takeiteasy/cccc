// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers -O3
// CCCC_MATRIX_SKIP: depends on -O3 specifically; the matrix only ever runs -O0 + one -f pass.
// See test_checked_pointers_assume_bounds_member_obj_hoist_fusion_o2.c --
// same proof at -O3.

struct S {
    int n;
    int * [[cccc::array, cccc::count(n)]] p;
};

int main(void) {
    struct S arr[2] = {{4, 0}, {10, 0}};
    struct S s = {4, (int[4]){1, 2, 3, 4}};
    volatile int k = 1;
    arr[k].p = s.p; // arr[1].n == 10, wider than s's own count(4) -- traps
    return arr[k].p[0];
}
