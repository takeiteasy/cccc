// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -Wnot-a-warning
// CCCC_EXPECT_STDERR: unknown warning option '-Wnot-a-warning'
int main(void) { return 42; }
