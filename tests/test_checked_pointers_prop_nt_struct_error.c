// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers
// #939/#943: CHKNTZ propagates through a checked-bounds-propagation
// candidate (#919/#941/#942) the same way CHKNT already does
// (tests/test_checked_pointers_prop_nt_error.c) -- a non-zero-byte store
// into the widened terminator slot through a propagated struct-typed
// pointer traps exactly like the direct-access case.

typedef struct {
    int  a;
    char b;
} Option;

int main(void) {
    int n = 2;
    Option *[[cccc::ntarray, cccc::count(n)]] tbl =
        (Option[3]){{1, 'a'}, {2, 'b'}, {0, 0}};
    Option *q = tbl;
    q[2]      = (Option){
        1,
        0}; // now traps via CHKNTZ through `q`, same as through `tbl` directly
    return 0;
}
