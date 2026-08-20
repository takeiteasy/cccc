// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers -O2
// CCCC_MATRIX_SKIP: depends on -O2 specifically; the matrix only ever runs -O0
// + one -f pass. The indexed load/store fusion in codegen.c
// (emit_indexed_load_if_possible) elides the address computation entirely at
// -O2+, which would bypass CHKR the same way it bypasses CHKP3/CHKT3 if left
// ungated (#484's "fusion hazard to verify, not assume"). CCCC_CHECKED_BOUNDS
// was added to CCCC_FUSION_UNSAFE_FLAGS to disable the fusion for
// checked-bounds derefs; this proves the out-of-bounds access still traps at
// -O2 rather than silently reading/writing past the declared count.

int main(void) {
    int n                                  = 4;
    int *[[cccc::array, cccc::count(n)]] a = (int[4]){1, 2, 3, 4};
    volatile int i                         = 4;
    int          x                         = a[i];
    return x;
}
