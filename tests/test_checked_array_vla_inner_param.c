// CCCC_FLAGS: --checked-pointers
// A checked array parameter with variable-length inner dimensions is bounded
// by its own lengths, on every dimension.

static int get(int m, int n, int a _Checked[m][n], int i, int j) {
    return a[i][j];
}

static int get3(int n, int a _Checked[3][n], int i, int j) {
    return a[i][j];
}

int main(void) {
    int m = 3, n = 4;
    int a _Checked[m][n];
    for (int i = 0; i < m; i++)
        for (int j = 0; j < n; j++)
            a[i][j] = i * 10 + j;
    if (get(m, n, a, 2, 3) != 23)
        return 1;
    int b _Checked[3][n];
    b[2][1] = 7;
    if (get3(n, b, 2, 1) != 7)
        return 2;
    return 42;
}
