// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @shared is only valid on #include
// @shared / [[cccc::shared]] must be rejected on directives other than #include.
#define @shared FOO
int main(void) { return 0; }
