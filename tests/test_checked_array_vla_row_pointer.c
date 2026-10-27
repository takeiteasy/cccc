// CCCC_NATIVE_SKIP: -c=native uses a propagated VLA-row pointer before declaring it (#1820)
// CCCC_FLAGS: --checked-pointers
// A pointer to a variable-length row keeps the row's length.

int main(void) {
    int m = 3, n = 4;
    int a _Checked[m][n];
    for (int i = 0; i < m; i++)
        for (int j = 0; j < n; j++)
            a[i][j] = i * 10 + j;
    int (*p)[n] = a;
    if (p[2][3] != 23 || p[1][0] != 10)
        return 1;
    return 42;
}
