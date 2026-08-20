// #943's CHKNT propagation is gated on the same --checked-pointers flag as
// the rest of the checked-pointer machinery -- the exact non-null write
// that traps in test_checked_pointers_prop_nt_error.c must run clean here
// and return 42.

int main(void) {
    int n                                     = 3;
    char *[[cccc::ntarray, cccc::count(n)]] s = (char[4]){'a', 'b', 'c', 0};
    char *q                                   = s;
    q[3] = 'x'; // no flag -- no check at all
    return q[3] == 'x' ? 42 : 1;
}
