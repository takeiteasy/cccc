// CCCC_FLAGS: -m --checked-pointers
// CCCC_EXPECT_STDERR: -m ignores VM runtime safety/debug options \(--checked-pointers\)
// CCCC_EXPECT_STDOUT: int f\(int \*p\)
// CCCC_REJECT_STDOUT: __cccc_tmp
//
// -m drops --checked-pointers before parsing, so the checked-pointer
// rewrites (bounds snapshots, dynamic-cast desugar) leave no compiler
// temporaries in the output.

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
