// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --std=c23
// CCCC_EXPECT_STDERR: static assertion failed
static_assert(0);
int main(void) { return 42; }
