// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --disable-ffi
// CCCC_EXPECT_STDOUT: return through a corrupted frame
// CCCC_EXPECT_STDERR: compile-time macro 'probe' aborted

// The slot after the saved bp is the return address.
[[cccc::comptime]]
void smash(void) {
    long long  slot = 0;
    long long *p    = &slot;
    for (int i = 1; i <= 8; i++) {
        if (p[i] == (long long)&p[i + 2]) { // saved bp: the caller's bp sits
                                            // after the return address
            p[i + 1] = 0x7fffffff0000;
            break;
        }
    }
}

[[cccc::comptime]]
Node *probe(void) {
    smash();
    return __builtin_ast_int_literal(1);
}

int main(void) {
    return probe();
}
