// EXPECT_COMPILE_ERROR
// JCC_FLAGS: -std=c17
// JCC_EXPECT_STDERR: binary integer literals are not available before C23
int main(void) { return 0b1010 == 10 ? 0 : 1; }
