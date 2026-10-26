// EXPECT_COMPILE_ERROR
// Only the outermost dimension of a checked array may be variable-length.

int main(void) {
    int n = 5;
    int a _Checked[3][n];
    return a[0][0];
}
