// EXPECT_COMPILE_ERROR
// JCC_EXPECT_STDERR: macro name must be an identifier
#undef 123
int main(void) { return 0; }
