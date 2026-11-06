// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: named parameter is required before '\.\.\.'

int main(void) {
    int (^f)() = ^(...) {
      return 1;
    };
    return f();
}
