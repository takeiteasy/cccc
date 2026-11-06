// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: too many arguments

int main(void) {
    int (^blk)(int a) = ^(int a) {
      return a;
    };
    return blk(40, 2);
}
