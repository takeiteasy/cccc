// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers -O2
// CCCC_MATRIX_SKIP: depends on -O2 specifically; the matrix only ever runs -O0
// + one -f pass. #937's CHKNT-on-RMW fix copies
// checked_bounds_lo/hi/checked_nt_terminator onto to_assign()'s synthesized
// `*tmp` store deref, which then reaches gen_expr's ND_ASSIGN case's
// promotion-guard branch (codegen.c:5111-5134, not the indexed-fusion branch
// #923's own nt_fusion_o2.c exercises) -- a distinct fusion hazard, since a
// promoted local store bypasses CHKNT entirely unless checked_bounds_lo AND
// checked_bounds_hi are both set. Proves a non-null RMW write into the widened
// terminator slot still traps at -O2 rather than silently corrupting the
// invariant.

int main(void) {
    int n                                     = 3;
    char *[[cccc::ntarray, cccc::count(n)]] s = (char[4]){'a', 'b', 'c', 0};
    volatile int i                            = 3; // the terminator slot
    s[i] += 1; // non-null RMW -- must still trap under fusion
    return 0;
}
