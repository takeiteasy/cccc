// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --asm-passthru
// CCCC_EXPECT_STDERR: cannot be a bit-field
//
// A bit-field has no address, so a memory constraint cannot name it.

struct S { int a : 3; };

int main(void) {
    struct S s = {0};
    __asm__ volatile("" : "=m"(s.a));
    return 42;
}
