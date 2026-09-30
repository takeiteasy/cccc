// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: calling 'external'.*no definition
int external(int);
[[cccc::kernel]] static int k(int x) { return external(x); }
int main(void) { return 42; }
