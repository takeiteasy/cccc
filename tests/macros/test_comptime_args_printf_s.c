// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDOUT: call to 'snprintf' argument 3
// CCCC_EXPECT_STDERR: compile-time macro 'probe' aborted
#include <stdio.h>

[[cccc::comptime]]
Node *probe(void) {
    char buf[8];
    snprintf(buf, sizeof buf, "%s", (const char *)8);
    return __builtin_ast_int_literal(1);
}

int main(void) {
    return probe();
}
