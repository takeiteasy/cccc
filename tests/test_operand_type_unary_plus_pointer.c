// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: invalid operand to unary \+
int main(void) {
    int x = 3; int *p = &x;
    long r = (long)+p;
    return (int)r;
}
