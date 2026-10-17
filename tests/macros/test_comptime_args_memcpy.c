// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDOUT: call to 'memcpy' argument 0: Write of 4 byte\(s\) at .* is outside VM memory
// CCCC_EXPECT_STDERR: compile-time macro 'probe' aborted
#include <string.h>

[[cccc::comptime]]
Node *probe(void) {
    char buf[4] = "abc";
    memcpy((void *)8, buf, 4);
    return __builtin_ast_int_literal(1);
}

int main(void) {
    return probe();
}
