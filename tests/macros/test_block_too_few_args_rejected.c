// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: no argument for parameter 'b'

int main(void) {
    int (^blk)(int a, int b) = ^(int a, int b) {
      return a + b;
    };
    return blk(42);
}
