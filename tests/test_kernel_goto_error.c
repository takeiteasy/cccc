// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: goto is not allowed
[[cccc::kernel]] static int k(int n) { goto out; out: return n; }
int main(void) { return k(42); }
