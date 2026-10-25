// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: invalid operands to &
int main(void) {
    double d = 1;
    long r = (long)(d & 1);
    return (int)r;
}
