// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: recursion through 'a'.*kernel code \(a -> b\)
static int a(int x);
static int b(int x) { return a(x - 1); }
[[cccc::kernel]] static int a(int x) { return x ? b(x) : 0; }
int main(void) { return a(1) + 42; }
