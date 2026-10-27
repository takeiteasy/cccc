// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers
// CCCC_NATIVE_SKIP: -c=native uses a propagated VLA-row pointer before declaring it (#1820)
// The row length bounds the column index through a row pointer.

int main(void) {
    int m = 3, n = 4;
    int a _Checked[m][n];
    int (*p)[n]    = a;
    volatile int j = n;
    return p[1][j];
}
