// CCCC_FLAGS: -Wignored-features --std=c11
// CCCC_EXPECT_STDERR: warning: '_Thread_local' is parsed but ignored
_Thread_local int x;
int main(void) { return 42; }
