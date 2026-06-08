// JCC_FLAGS: -Wattributes --std=c23
// JCC_EXPECT_STDERR: warning: unknown attribute 'nodiscard' ignored
int x [[nodiscard]];
int main(void) { return 42; }
