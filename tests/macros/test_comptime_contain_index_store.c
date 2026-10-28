// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --disable-ffi
// CCCC_EXPECT_STDOUT: Write of 4 byte\(s\) at .* is outside VM memory
// CCCC_EXPECT_STDERR: compile-time macro 'poke' aborted

[[cccc::comptime]]
Node *poke(void) {
    volatile int *p = (volatile int *)0;
    int           i = 2;
    p[i]            = 1;
    return __builtin_ast_int_literal(1);
}

int main(void) {
    return poke();
}
