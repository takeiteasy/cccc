// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: kernel code \(k -> leaf\)
static int g;
static int leaf(void) { return g; }
[[cccc::kernel]] static int k(void) { return leaf(); }
int main(void) { return 42; }
