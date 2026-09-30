// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: inline asm
[[cccc::kernel]] static void k(void) { __asm__ volatile("nop"); }
int main(void) { return 42; }
