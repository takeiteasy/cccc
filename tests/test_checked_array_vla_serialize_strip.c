// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: int a\[__cccc_tmp[0-9]+\]
// CCCC_REJECT_STDOUT: _Checked
//
// A checked VLA serializes as an ordinary VLA.

int main(void) {
    int n = 4;
    int a _Checked[n];
    a[1] = 1;
    return a[1] == 1 ? 42 : 1;
}
