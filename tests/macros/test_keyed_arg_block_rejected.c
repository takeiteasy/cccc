// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: keyed arguments need a direct call to a function

int main(void) {
    int (^blk)(int a) = ^(int a) {
      return a;
    };
    return blk(.a = 42);
}
