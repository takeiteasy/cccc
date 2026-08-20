// A struct member's checked-pointer bounds still parse and resolve (#921)
// without --checked-pointers, but CHKR itself stays opt-in for the member
// path too -- the exact same out-of-bounds member access that traps in
// test_suite_checked_pointers.c must run clean here and return 42.

struct S {
    int n;
    int *[[cccc::array, cccc::count(n)]] p;
};

int main(void) {
    struct S     s = {4, (int[4]){1, 2, 3, 4}};
    volatile int i =
        4; // one past the declared count -- no check without the flag
    int x = s.p[i];
    (void)x;
    return 42;
}
