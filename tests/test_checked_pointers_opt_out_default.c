// Checked-pointer types and bounds attributes always parse and the
// compile-time arithmetic rule is always on, but CHKR itself is opt-in
// (#770/#484): without --checked-pointers, the exact same out-of-bounds
// access that traps in test_suite_checked_pointers.c must run clean here
// and return 42.

int main(void) {
    int n                                  = 5;
    int *[[cccc::array, cccc::count(n)]] a = (int[5]){1, 2, 3, 4, 5};
    volatile int i =
        5; // one past the declared count -- no check without the flag
    int x = a[i];
    (void)x;
    return 42;
}
