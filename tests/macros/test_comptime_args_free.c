// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDOUT: free: Write of 1 byte\(s\) at .* is outside VM memory
// CCCC_EXPECT_STDERR: compile-time macro 'probe' aborted
#include <stdlib.h>

[[cccc::comptime]]
Node *probe(void) {
    free((void *)8);
    return __builtin_ast_int_literal(1);
}

int main(void) {
    return probe();
}
