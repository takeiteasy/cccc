// CCCC_FLAGS: -m --checked-pointers
// CCCC_REJECT_STDERR: ignores VM runtime safety/debug options
// CCCC_REJECT_STDOUT: __cccc_chkr\(\(int \*\)\(\(char \*\)\(int \*\)a \+ \(long\)4
// CCCC_EXPECT_STDOUT: __cccc_chkr\(\(int \*\)\(\(char \*\)\(int \*\)a \+ \(long\)i
// CCCC_EXPECT_STDOUT: __cccc_chkr\(\(int \*\)\(\(char \*\)\(int \*\)p \+ \(long\)2
//
// -m drops the lowered check for a provably in-bounds constant index into a
// checked array, and keeps it for a runtime index and for a pointer base.

int f(int *[[cccc::array, cccc::count(3)]] p) {
    return p[2];
}

int main(void) {
    int a        _Checked[5] = {1, 2, 3, 4, 5};
    volatile int i           = 4;
    return f(a) + a[4] + a[i] + 35;
}
