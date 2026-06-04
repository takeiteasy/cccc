// EXPECT_COMPILE_ERROR
// JCC_EXPECT_STDERR: stray #else without matching #if
#else
int main(void) { return 0; }
