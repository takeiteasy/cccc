// #944's CHKAB assignment-bounds-implication check is gated on the same
// --checked-pointers flag as the rest of the checked-pointer machinery --
// the exact assignment that traps in
// test_checked_pointers_assume_bounds_error.c must run clean here and
// return 42 (the OOB read itself is also unchecked without the flag).

int main(void) {
    int * [[cccc::array, cccc::count(4)]] p = (int[4]){1, 2, 3, 4};
    int * [[cccc::array, cccc::count(10)]] q;
    q = p; // no flag -- no check at all
    (void)q;
    return 42;
}
