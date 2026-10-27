// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers
// The row length bounds the column index through a row pointer.

int main(void) {
    int m = 3, n = 4;
    int a _Checked[m][n];
    int (*p)[n]    = a;
    volatile int j = n;
    return p[1][j];
}
