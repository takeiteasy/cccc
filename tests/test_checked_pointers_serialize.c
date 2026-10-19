// CCCC_FLAGS: -m --checked-pointers
// CCCC_REJECT_STDERR: ignores VM runtime safety/debug options
// CCCC_EXPECT_STDOUT: static inline void \*__cccc_chkr\(
// CCCC_EXPECT_STDOUT: return \(\*\(int \*\)\(__cccc_chkr\(
// CCCC_REJECT_STDOUT: \[\[cccc::array
//
// -m lowers --checked-pointers checks into calls to the emitted helpers and
// still strips the checked-pointer attributes.

int f(int *[[cccc::array, cccc::count(3)]] p) {
    return p[0];
}

int main(void) {
    int x[3] = {1, 2, 3};
    return f(x) + 41;
}
