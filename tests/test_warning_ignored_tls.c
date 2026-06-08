// JCC_FLAGS: -Wignored-features --std=c11
// JCC_EXPECT_STDERR: warning: '_Thread_local' is parsed but ignored
_Thread_local int x;
int main(void) { return 42; }
