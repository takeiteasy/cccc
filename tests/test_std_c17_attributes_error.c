// EXPECT_COMPILE_ERROR
// JCC_FLAGS: -std=c17
// JCC_EXPECT_STDERR: '\[\[...\]\]' attributes are not available before C23
[[nodiscard]] int f(void) { return 1; }
