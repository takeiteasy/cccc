// CCCC_FLAGS: -m -Wignored-features
// CCCC_EXPECT_STDERR: config\(checked_pointers\) has no effect in -m mode
// CCCC_REJECT_STDERR: ignores VM runtime safety/debug options
// CCCC_EXPECT_STDOUT: int f\(int \*p\)
// CCCC_REJECT_STDOUT: __cccc_tmp
//
// Under -m, #pragma cccc config(...) is a warned no-op, as under -c=native:
// it never enables the checked-pointer rewrites, so no compiler temporaries
// reach the output.

#pragma cccc config(checked_pointers)

int f(int *[[cccc::array, cccc::count(3)]] p) {
    int *[[cccc::array, cccc::count(3)]] q =
        (int *[[cccc::array, cccc::count(3), cccc::dynamic]])p;
    int *[[cccc::array, cccc::count(3)]] r = q + 1;
    return q[0] + r[0];
}

int main(void) {
    int x[3] = {1, 2, 3};
    return f(x) + 39;
}
