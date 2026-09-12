// EXPECT_COMPILE_ERROR
// #487 v1 boundary: a multidimensional checked array is rejected outright --
// find_checked_base() stops at the inner ND_DEREF for a[i][j], so a silently
// accepted `_Checked[3][5]` would parse clean and enforce nothing, which is
// worse than refusing it.

int main(void) {
    int a _Checked[3][5];
    return a[0][0];
}
