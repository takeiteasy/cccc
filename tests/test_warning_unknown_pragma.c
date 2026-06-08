// JCC_FLAGS: -Wcpp
// JCC_EXPECT_STDERR: warning: unknown pragma ignored
#pragma GCC system_header
int main(void) { return 42; }
