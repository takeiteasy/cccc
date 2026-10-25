// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: invalid operands to \*
int main(void) {
    int x = 3; int *p = &x;
    long r = (long)(p * 2);
    return (int)r;
}
