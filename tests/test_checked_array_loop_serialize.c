// CCCC_FLAGS: -m --checked-pointers
// CCCC_REJECT_STDERR: ignores VM runtime safety/debug options
// CCCC_REJECT_STDOUT: __cccc_chkr\(\(int \*\)\(\(char \*\)\(int \*\)a \+
// CCCC_EXPECT_STDOUT: __cccc_chkr\(\(int \*\)\(\(char \*\)\(int \*\)b \+
//
// -m drops the lowered check for a loop index proven inside a checked array,
// and keeps it where the bound is only known at run time.

int main(void) {
    int a        _Checked[5] = {1, 2, 3, 4, 5};
    int b        _Checked[5] = {1, 2, 3, 4, 5};
    int          sum         = 0;
    volatile int n           = 5;
    for (int i = 0; i < 5; i++)
        sum += a[i];
    for (int i = 0; i < n; i++)
        sum += b[i];
    return sum + 12;
}
