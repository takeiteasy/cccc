// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: invalid operand to unary ~
int main(void) {
    
    long r = (long)~1.5;
    return (int)r;
}
