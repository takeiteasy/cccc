// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --asm-passthru
// CCCC_EXPECT_STDERR: test_asm_passthru_host_error\.c:9:[\s\S]*native compilation failed
//
// The host compiler rejects the constraint; its diagnostics reach stderr.

int main(void) {
    int x = 0;
    __asm__ volatile("" : "=Z9"(x));
    return 42;
}
