// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers
// #487: decaying a checked array into a plain unchecked pointer local still
// checks through it, via the existing #919 bounds-propagation pass -- q's
// declaration (`int *q = a;`) is a FULL propagation candidate rooted at a's
// declared bounds, so q[i] is checked against a's snapshot exactly like
// `int *q = p + 0;` would be for a checked pointer p.

int main(void) {
    int a        _Checked[10];
    int         *q = a;
    volatile int i = 10;
    q[i]           = 1;
    return 42;
}
