// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --disable-ffi
// CCCC_EXPECT_STDOUT: Write of 24 byte\(s\) at .* is outside VM memory
// CCCC_EXPECT_STDERR: compile-time macro 'clobber' aborted

struct Big { long a, b, c; };

[[cccc::comptime]]
Node *clobber(void) {
    struct Big src = {1, 2, 3};
    *(struct Big *)8 = src;
    return __builtin_ast_int_literal(1);
}

int main(void) {
    return clobber();
}
