// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: method receiver type must be a struct or union
[[cccc::method(int)]]
int f(int *self) { return 0; }
int main(void) { return 0; }
