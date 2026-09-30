// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: a call through a function pointer
static int one(int x) { return x; }
[[cccc::kernel]] static int k(int (*f)(int)) { return f(1); }
int main(void) { return k(one) + 41; }
