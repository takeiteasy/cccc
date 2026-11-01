// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers
// The inner index of a nested loop is checked on its own: the outer loop is
// in range but the inner one runs past the row.

int main(void) {
    int g _Checked[3][5] = {{0}};
    int   sum            = 0;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 6; j++)
            sum += g[i][j];
    return 42;
}
