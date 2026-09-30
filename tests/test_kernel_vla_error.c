// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: variable-length array
[[cccc::kernel]] static int k(int n) { int a[n]; a[0] = n; return a[0]; }
int main(void) { return k(42); }
