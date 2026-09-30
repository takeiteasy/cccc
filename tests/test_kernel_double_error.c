// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: double or long double
[[cccc::kernel]] static int k(int n) { double d = n; return (int)d; }
int main(void) { return k(42); }
