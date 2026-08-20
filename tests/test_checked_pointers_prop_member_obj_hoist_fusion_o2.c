// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers -O2
// CCCC_MATRIX_SKIP: depends on -O2 specifically; the matrix only ever runs -O0
// + one -f pass. #947 extends #945's per-access object-expression hoist to
// #919's propagation snapshot: `q = arr[k].p;` hoists `arr[k]` into a single
// temp shared by the snapshot's lo and hi stores, rather than
// test_checked_pointers_prop_fusion_o2.c's trivial (bare-variable) source.
// This proves the out-of-bounds access through a pointer propagated from a
// runtime-indexed array-of-structs member still traps at -O2.

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
    int         *q = arr[k].p;
    volatile int i = 4;
    int          x = q[i];
    return x;
}
