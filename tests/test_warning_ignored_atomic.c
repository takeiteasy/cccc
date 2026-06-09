// CCCC_FLAGS: -Wignored-features
// CCCC_EXPECT_STDERR: warning: '_Atomic' is parsed but non-atomic
_Atomic int x;
int main(void) { return 42; }
