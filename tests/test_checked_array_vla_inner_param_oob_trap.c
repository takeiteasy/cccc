// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers
// The inner length of a checked array parameter bounds the column index.

static int get(int m, int n, int a _Checked[m][n], int i, int j) {
    return a[i][j];
}

int main(void) {
    int m = 3, n = 4;
    int a _Checked[m][n];
    volatile int j = n;
    return get(m, n, a, 1, j);
}
