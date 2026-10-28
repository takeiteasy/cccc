// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --disable-ffi
// CCCC_EXPECT_STDOUT: Read of 4 byte\(s\) at .* is outside VM memory
// CCCC_EXPECT_STDERR: compile-time macro 'peek' aborted

[[cccc::comptime]]
Node *peek(void) {
    volatile int *p = (volatile int *)0;
    int           i = 2;
    int           v = p[i];
    return __builtin_ast_int_literal(v);
}

int main(void) {
    return peek();
}
