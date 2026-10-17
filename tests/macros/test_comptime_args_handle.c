// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDOUT: call to 'fputs' argument 1
// CCCC_EXPECT_STDERR: compile-time macro 'probe' aborted
#include <stdio.h>

[[cccc::comptime]]
Node *probe(void) {
    fputs("x", (FILE *)8);
    return __builtin_ast_int_literal(1);
}

int main(void) {
    return probe();
}
