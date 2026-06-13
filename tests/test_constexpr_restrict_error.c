// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --std=c23
// CCCC_EXPECT_STDERR: constexpr object has unsupported type or qualifiers
int target;
constexpr int *restrict p = &target;
int main(void) { return *p; }
