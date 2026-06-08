// JCC_FLAGS: -Wattributes --std=c23
// JCC_EXPECT_STDERR: warning: unknown attribute 'foobar' ignored
int x [[foobar]];
int main(void) { return 42; }
