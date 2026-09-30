// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: static local variable
[[cccc::kernel]] static int k(void) { static int n; return ++n; }
int main(void) { return 42; }
