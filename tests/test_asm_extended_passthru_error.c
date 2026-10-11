// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --asm-passthru
// CCCC_EXPECT_STDERR: extended asm operands are not supported with --asm-passthru
//
// --asm-passthru compiles a zero-argument thunk, so a template that refers to
// operands cannot be honoured.

int main(void) {
    int x = 1;
    __asm__ volatile("" : "+r"(x));
    return 42;
}
