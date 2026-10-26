// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: unchecked array local variable
// An unchecked outer array of checked rows is not allowed: the outermost
// array type must be checked.

typedef int Row _Checked[5];

[[cccc::checked]]
int f(void) {
    Row a[3];
    return a[0][0];
}

int main(void) {
    return 42;
}
