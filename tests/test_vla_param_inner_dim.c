// A parameter whose pointee is variable-length reads the other parameters.

static int sum_rows(int n, int a[][n], int rows) {
    int s = 0;
    for (int i = 0; i < rows; i++)
        for (int j = 0; j < n; j++)
            s += a[i][j];
    return s;
}

static int stride(int n, int (*a)[n]) {
    return (int)((char *)(a + 1) - (char *)a);
}

static int deep(int n, int m, int a[][n][m]) {
    return a[1][2][3] + (int)sizeof(a[0]) + (int)(&a[1] - &a[0]);
}

int main(void) {
    int c[2][5] = {{1, 2, 3, 4, 5}, {6, 7, 8, 9, 10}};
    if (sum_rows(5, c, 2) != 55)
        return 1;
    if (stride(5, c) != 5 * (int)sizeof(int))
        return 2;
    int b[2][3][4];
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 3; j++)
            for (int l = 0; l < 4; l++)
                b[i][j][l] = i * 100 + j * 10 + l;
    if (deep(3, 4, b) != 123 + 48 + 1)
        return 3;
    return 42;
}
