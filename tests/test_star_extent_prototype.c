// An unspecified extent `[*]` in a prototype parses, compiles natively, and a
// definition spells the real extents.

int f(int n, int a[n][*]);
int g(int m, int n, int (*a)[*][*]);
int (*fp)(int, int (*)[*]);
int main(void) {
    int b[2][3] = {{0, 0, 0}, {0, 0, 40}};
    return f(3, b) + g(2, 3, (int (*)[2][3])b) + 2;
}
int f(int n, int a[n][n]) {
    return a[1][2];
}
int g(int m, int n, int (*a)[m][n]) {
    return 0;
}
