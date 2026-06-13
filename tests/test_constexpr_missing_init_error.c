// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --std=c23
// CCCC_EXPECT_STDERR: constexpr object requires an initializer
constexpr int n;
int main(void) { return n; }
