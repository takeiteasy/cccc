// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDOUT: %n is not allowed at comptime
// CCCC_EXPECT_STDERR: compile-time macro 'probe' aborted
#include <stdio.h>

[[cccc::comptime]]
Node *probe(void) {
    char buf[8];
    int  n;
    snprintf(buf, sizeof buf, "ab%n", &n);
    return __builtin_ast_int_literal(n);
}

int main(void) {
    return probe();
}
