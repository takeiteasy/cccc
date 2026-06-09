// CCCC_FLAGS: -Wignored-features --std=c11
// CCCC_EXPECT_STDERR: warning: '__thread' is parsed but ignored
__thread int x;
int main(void) { return 42; }
