// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: \(char \*\)
// CCCC_REJECT_STDOUT: /\* init data \*[/]
// CCCC_REJECT_STDOUT: [+-]\s*\([A-Za-z_][\w ]*\*\)\(
// -m output has no placeholder initializer and no pointer-typed cast on a
// scaled offset.
int arr[5] = {1, 2, 3, 4, 5};
struct S {
    int   a;
    short b;
};
struct S    gs        = {7, 9};
int        *gptr      = &arr[2];
const char *gp        = "lit";
int         mat[2][3] = {{1, 2, 3}, {4, 5, 6}};
int main(void) {
    int *a = arr, *q = arr + 3;
    if (a[0] != 1 || q[-1] != 3)
        return 1;
    if (q - a != 3)
        return 2;
    if (gs.a != 7 || gs.b != 9)
        return 3;
    if (*gptr != 3)
        return 4;
    if (gp[0] != 'l')
        return 5;
    if (mat[1][2] != 6)
        return 6;
    return 42;
}
