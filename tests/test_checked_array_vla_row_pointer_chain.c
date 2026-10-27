// CCCC_FLAGS: --checked-pointers
// Two row pointers declared together keep the array's bounds.

int main(void) {
    int m = 3, n = 4;
    int a _Checked[m][n];
    for (int i = 0; i < m; i++)
        for (int j = 0; j < n; j++)
            a[i][j] = i * n + j;
    int (*p)[n] = a, (*q)[n] = p;
    return q[2][3] + 31;
}
