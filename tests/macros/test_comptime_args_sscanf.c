// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDOUT: call to 'sscanf' argument 2: Write of 4 byte\(s\) at .* is outside VM memory
// CCCC_EXPECT_STDERR: compile-time macro 'probe' aborted
#include <stdio.h>

[[cccc::comptime]]
Node *probe(void) {
    sscanf("12", "%d", (int *)8);
    return __builtin_ast_int_literal(1);
}

int main(void) {
    return probe();
}
