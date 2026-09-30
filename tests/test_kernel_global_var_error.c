// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: the global variable 'counter'
static int counter;
[[cccc::kernel]] static int k(void) { return ++counter; }
int main(void) { return 42; }
