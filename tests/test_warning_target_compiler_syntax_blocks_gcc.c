// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -E --output-compiler=gcc
// CCCC_EXPECT_STDERR: error: blocks are not supported by gcc \[-Wtarget-compiler\]
int f(void) {
    int (^b)(int) = ^(int y) {
      return y;
    };
    return b(1);
}
int main(void) {
    return 42;
}
