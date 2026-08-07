// #923's CHKNT terminator guard is gated on the same --checked-pointers
// flag as CHKR (#770/#484): without the flag, a non-null write into the
// widened terminator slot runs clean and returns 42, just like an
// out-of-bounds access does in test_checked_pointers_opt_out_default.c.

int main(void) {
    int n = 3;
    char * [[cccc::ntarray, cccc::count(n)]] s = (char[4]){'a', 'b', 'c', 0};
    s[n] = 'x'; // terminator slot, non-null -- no check without the flag
    return 42;
}
