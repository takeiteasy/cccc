// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers
// The array's row count bounds the row index through a row pointer.

int main(void) {
    int m = 3, n = 4;
    int a _Checked[m][n];
    int (*p)[n]    = a;
    volatile int i = m;
    return p[i][0];
}
