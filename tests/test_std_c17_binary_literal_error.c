// JCC_FLAGS: -std=c17 -Wpedantic
// JCC_EXPECT_STDERR: warning: binary integer literals are a C23 extension \[-Wpedantic\]
int main(void) { return 0b101010 == 42 ? 42 : 1; }
