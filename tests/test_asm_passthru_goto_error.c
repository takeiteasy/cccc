// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --asm-passthru
// CCCC_EXPECT_STDERR: asm goto is not supported with --asm-passthru
//
// asm goto cannot return into VM code.

int main(void) {
    __asm__ goto("" ::: : done);
done:
    return 42;
}
