// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --std=c23
// CCCC_EXPECT_STDERR: constexpr object has unsupported type or qualifiers
constexpr volatile int n = 1;
int main(void) { return n; }
