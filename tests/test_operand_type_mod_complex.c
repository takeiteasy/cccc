// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: invalid operands to %
int main(void) {
    double _Complex z = 1;
    long r = (long)(z % 2);
    return (int)r;
}
