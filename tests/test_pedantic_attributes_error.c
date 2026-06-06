// EXPECT_COMPILE_ERROR
// JCC_FLAGS: --std=c17 -Wpedantic -Werror=pedantic
// JCC_EXPECT_STDERR: error: '\[\[...\]\]' attributes are a C23 extension \[-Wpedantic\]
int [[nodiscard]] f(void) { return 42; }
int main(void) { return f(); }
