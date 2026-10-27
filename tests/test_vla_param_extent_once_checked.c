// CCCC_FLAGS: --checked-pointers
// A parameter extent with side effects is evaluated once per call.

static int calls;

static int rows(void) {
    return ++calls, 4;
}

static int pick(int (*a)[rows()], int i, int j) {
    return a[i][j];
}

static int pick2(int n, int (*a)[n][rows()], int i, int j) {
    int (*q)[n][4] = (int (*)[n][4])a;
    return (*q)[i][j] + (&a != 0);
}

int main(void) {
    int b[2][4]    = {};
    b[1][2]        = 40;
    int c[1][3][4] = {};
    c[0][2][1]     = 0;
    int x          = pick(b, 1, 2);
    if (calls != 1)
        return 1;
    int y = pick2(3, c, 2, 1);
    if (calls != 2)
        return 2;
    return x + y + 1;
}
