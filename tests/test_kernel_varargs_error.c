// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: a variadic function is not allowed
[[cccc::kernel]] static int k(int n, ...) { return n; }
int main(void) { return k(42); }
