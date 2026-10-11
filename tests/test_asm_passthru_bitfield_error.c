// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --asm-passthru
// CCCC_EXPECT_STDERR: not addressable with --asm-passthru
//
// A bit-field has no address to pass.

struct S { int a : 3; };

int main(void) {
    struct S s = {0};
    __asm__ volatile("" : "+r"(s.a));
    return 42;
}
