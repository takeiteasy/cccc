// #919's bounds propagation itself is gated on --checked-pointers at PARSE
// time (unlike the rest of the checked-pointer machinery, which populates
// its fields unconditionally and gates only CHKR's emission at codegen) --
// the snapshot temps and stores are not even synthesized without the flag.
// The exact same out-of-bounds access through a propagated pointer that
// traps in test_suite_checked_pointers.c must run clean here and return 42.

int main(void) {
    int n = 4;
    int * [[cccc::array, cccc::count(n)]] p = (int[4]){1, 2, 3, 4};
    int *q = p + 0;
    volatile int i = 4; // one past the declared count -- no check without the flag
    int x = q[i];
    (void)x;
    return 42;
}
