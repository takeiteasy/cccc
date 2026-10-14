// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --watch-debounce=soon
// CCCC_EXPECT_STDERR: --watch-debounce needs milliseconds

int main(void) { return 42; }
