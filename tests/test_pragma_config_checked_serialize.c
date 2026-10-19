// CCCC_FLAGS: -m -Wignored-features
// CCCC_REJECT_STDERR: has no effect
// CCCC_EXPECT_STDOUT: __cccc_chkr\(
//
// #pragma cccc config(checked_pointers) enables the checks under -m, like
// the --checked-pointers option.

#pragma cccc config(checked_pointers)

int f(int *[[cccc::array, cccc::count(3)]] p) {
    return p[0];
}

int main(void) {
    int x[3] = {1, 2, 3};
    return f(x) + 41;
}
