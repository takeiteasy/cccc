// CCCC_FLAGS: -E --output-compiler=clang
// CCCC_REJECT_STDERR: target-compiler|error
// CCCC_EXPECT_STDOUT: \^\(int y\)
int f(void) {
    int (^b)(int) = ^(int y) {
      return y;
    };
    return b(1);
}
int main(void) {
    return 42;
}
