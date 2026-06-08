// JCC_FLAGS: -Wignored-features --std=c11
// JCC_EXPECT_STDERR: warning: '__thread' is parsed but ignored
__thread int x;
int main(void) { return 42; }
