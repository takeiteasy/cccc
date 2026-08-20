// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers -O2
// CCCC_MATRIX_SKIP: depends on -O2 specifically; the matrix only ever runs -O0
// + one -f pass. #939's CHKNTZ (null-terminator guard for the memcpy-lowered
// ntarray pointees CHKNT itself cannot reach -- struct/union and wide
// _BitInt/_Decimal) is, like CHKNT
// (tests/test_checked_pointers_nt_fusion_o2.c), emitted from the store path
// rather than gen_addr. Proves a non-zero-byte write into the widened
// terminator slot still traps at -O2.

typedef struct {
    int  a;
    char b;
} Option;

int main(void) {
    int n = 2;
    Option *[[cccc::ntarray, cccc::count(n)]] tbl =
        (Option[3]){{1, 'a'}, {2, 'b'}, {0, 0}};
    volatile int i = 2;              // the terminator slot
    tbl[i]         = (Option){1, 0}; // non-zero -- must still trap under fusion
    return 0;
}
