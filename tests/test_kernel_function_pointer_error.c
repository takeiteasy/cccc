// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: a function pointer is not allowed in kernel code
static int one(int x) { return x; }
[[cccc::kernel]] static int k(int x) { int (*f)(int) = one; return x; }
int main(void) { return k(1) + 41; }
