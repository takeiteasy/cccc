// EXPECT_COMPILE_ERROR
// JCC_FLAGS: -Wnot-a-warning
// JCC_EXPECT_STDERR: unknown warning option '-Wnot-a-warning'
int main(void) { return 42; }
