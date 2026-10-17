// CCCC_FLAGS: --disable-ffi
// CCCC_EXPECT_STDOUT: Error type: FFI Disabled
// A libc call at comptime is still refused under --disable-ffi (it yields 0).
#include[[cccc::comptime]] <stdlib.h>

[[cccc::comptime]]
Node *probe(void) {
    return __builtin_ast_int_literal(abs(-3));
}

int main(void) {
    return probe() + 42;
}
