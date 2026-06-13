// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --std=c23
// CCCC_EXPECT_STDERR: constexpr is only supported for object definitions
constexpr int f(void) { return 42; }
int main(void) { return f(); }
