// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: recursion through 'f'
[[cccc::kernel]] static int f(int x) { return x ? f(x - 1) : 0; }
int main(void) { return f(1) + 42; }
