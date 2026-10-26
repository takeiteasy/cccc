// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: int __cccc_tmp[0-9]+\[4\]
// CCCC_REJECT_STDOUT: _Checked
//
// A checked compound literal serializes as an ordinary array literal.

int main(void) {
    int *p = (int _Checked[4]){1, 2, 3, 4};
    return p[1] == 2 ? 42 : 1;
}
