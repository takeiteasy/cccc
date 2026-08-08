// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers -O3
// CCCC_MATRIX_SKIP: depends on -O3 specifically; the matrix only ever runs -O0 + one -f pass.
// -O3 counterpart of test_checked_pointers_nt_struct_fusion_o2.c -- see
// that file's comment.

typedef struct { int a; char b; } Option;

int main(void) {
    int n = 2;
    Option * [[cccc::ntarray, cccc::count(n)]] tbl =
        (Option[3]){{1, 'a'}, {2, 'b'}, {0, 0}};
    volatile int i = 2; // the terminator slot
    tbl[i] = (Option){1, 0}; // non-zero -- must still trap under fusion
    return 0;
}
