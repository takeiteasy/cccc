// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: int a\[3\]\[5\]
// CCCC_REJECT_STDOUT: _Checked
//
// A multidimensional checked array serializes as a plain 2-D array.

int main(void) {
    int a _Checked[3][5];
    a[1][2] = 1;
    return a[1][2] == 1 ? 42 : 1;
}
