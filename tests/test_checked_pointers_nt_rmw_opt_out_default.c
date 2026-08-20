// #937's CHKNT-on-RMW propagation is gated on the same --checked-pointers
// flag as CHKR/CHKNT themselves (#770/#484/#923): without the flag, a
// non-null read-modify-write into the widened terminator slot runs clean
// and returns 42, just like test_checked_pointers_nt_opt_out_default.c's
// plain-assignment case.

int main(void) {
    int n                                     = 3;
    char *[[cccc::ntarray, cccc::count(n)]] s = (char[4]){'a', 'b', 'c', 0};
    s[n] += 1; // terminator slot, non-null RMW -- no check without the flag
    return 42;
}
