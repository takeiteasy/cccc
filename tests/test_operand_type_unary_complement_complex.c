// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: invalid operand to unary ~
int main(void) {
    double _Complex z = 1;
    long r = (long)~z;
    return (int)r;
}
