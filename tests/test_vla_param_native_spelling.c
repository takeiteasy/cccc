// A parameter whose extents read earlier parameters compiles under -c=native.

static int at(int m, int n, int (*a)[m][n], int i, int j) {
    return a[0][i][j];
}

static int tail(int n, int a[][n]) {
    return a[1][n - 1];
}

int proto_first(int n, int (*a)[n]);

int main(void) {
    int b[1][3][4]                                        = {};
    b[0][1][2]                                            = 30;
    int c[2][5]                                           = {};
    c[1][4]                                               = 7;
    int d[2][6]                                           = {};
    d[1][0]                                               = 5;

    int (*fp)(int m, int n, int (*a)[m][n], int i, int j) = at;

    int sum = fp(3, 4, b, 1, 2) + tail(5, c) + proto_first(6, d);
    return sum;
}

int proto_first(int n, int (*a)[n]) {
    return a[1][0];
}
