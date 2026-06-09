// CCCC_FLAGS: --std=c17 -Wpedantic
// CCCC_EXPECT_STDERR: warning: '\[\[...\]\]' attributes are a C23 extension \[-Wpedantic\]
int [[nodiscard]] f(void) { return 42; }
int main(void) { return f(); }
