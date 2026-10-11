// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --asm-passthru
// CCCC_EXPECT_STDERR: aggregate asm operand needs a memory constraint
//
// A struct needs a memory constraint; a register would copy it.

typedef struct { int a, b; } Pair;

int main(void) {
    Pair p = {1, 2};
    __asm__ volatile("" : : "r"(p));
    return 42;
}
