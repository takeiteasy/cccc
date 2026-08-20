// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers -O2
// CCCC_MATRIX_SKIP: depends on -O2 specifically; the matrix only ever runs -O0
// + one -f pass. #945 hoists a non-trivial member-access object expression
// (here, arr[k]'s runtime index `k`) into a single compiler-generated temp
// shared by lo and hi -- test_checked_pointers_member_fusion_o2.c only proves
// the TRIVIAL (bare-variable) object-expression case survives -O2's indexed
// load/store fusion, which wouldn't have caught a regression specific to the
// hoisted temp itself (e.g. the temp's store getting fused away, or a stale
// value surviving across optimization passes). This proves the out-of-bounds
// access through a runtime-indexed array-of-structs member still traps
// at -O2.

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
