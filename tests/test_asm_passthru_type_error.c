// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --asm-passthru
// CCCC_EXPECT_STDERR: asm operand type is not supported with --asm-passthru
//
// long double has no thunk spelling.

int main(void) {
    long double ld = 1.0L;
    __asm__ volatile("" : "+r"(ld));
    return 42;
}
