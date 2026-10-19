// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: int f\(int \*p\)
// CCCC_REJECT_STDOUT: __cccc_chk
// CCCC_REJECT_STDOUT: __cccc_tmp
//
// Without --checked-pointers, -m emits no checks, helpers or compiler
// temporaries for checked-pointer code.

int f(int *[[cccc::array, cccc::count(3)]] p) {
    int *[[cccc::array, cccc::count(3)]] q =
        (int *[[cccc::array, cccc::count(3), cccc::dynamic]])p;
    int *[[cccc::array, cccc::count(3)]] r = q + 1;
    return *p + q[0] + r[0];
}

int main(void) {
    int x[3] = {1, 2, 3};
    return f(x) + 38;
}
