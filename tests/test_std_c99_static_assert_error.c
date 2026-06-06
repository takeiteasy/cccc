// EXPECT_COMPILE_ERROR
// JCC_FLAGS: --std=c99
// JCC_EXPECT_STDERR: '_Static_assert' is not available before C11
_Static_assert(sizeof(int) >= 4, "int must be at least 4 bytes");
int main(void) { return 0; }
