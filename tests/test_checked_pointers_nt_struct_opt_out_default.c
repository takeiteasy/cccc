// #939's CHKNTZ terminator guard for struct-typed ntarray pointees is gated
// on the same --checked-pointers flag as CHKR/CHKNT: without the flag, a
// non-zero-byte write into the widened terminator slot runs clean and
// returns 42, just like tests/test_checked_pointers_nt_opt_out_default.c's
// scalar case.

typedef struct {
    int  a;
    char b;
} Option;

int main(void) {
    int n = 2;
    Option *[[cccc::ntarray, cccc::count(n)]] tbl =
        (Option[3]){{1, 'a'}, {2, 'b'}, {0, 0}};
    tbl[n] = (Option){
        1, 0}; // terminator slot, non-zero -- no check without the flag
    return 42;
}
