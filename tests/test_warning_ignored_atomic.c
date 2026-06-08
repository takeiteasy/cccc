// JCC_FLAGS: -Wignored-features
// JCC_EXPECT_STDERR: warning: '_Atomic' is parsed but non-atomic
_Atomic int x;
int main(void) { return 42; }
