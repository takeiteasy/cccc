// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers -O2
// CCCC_MATRIX_SKIP: depends on -O2 specifically; the matrix only ever runs -O0 + one -f pass.
// #919's propagated deref (q[i], q snapshotted from p) reaches its address
// through the same ND_ADD/ND_VAR shape as a direct checked-pointer access,
// but the checked_bounds_lo/hi fields are only populated post-parse by
// propagate_checked_bounds()'s walk 3 -- test_checked_pointers_fusion_o2.c
// only proves the direct-access case survives -O2's indexed load/store
// fusion (match_indexed_addr() in src/codegen.c), which would not have
// caught a regression specific to a propagated deref. This proves the
// out-of-bounds access through a propagated pointer still traps at -O2.

int main(void) {
    int n = 4;
    int * [[cccc::array, cccc::count(n)]] p = (int[4]){1, 2, 3, 4};
    int *q = p + 0;
    volatile int i = 4;
    int x = q[i];
    return x;
}
