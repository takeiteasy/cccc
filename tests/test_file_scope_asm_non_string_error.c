// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: expected asm string literal
//
// File-scope asm takes a string literal; operands are only valid in a function.

__asm__(42);

int main(void) {
    return 42;
}
