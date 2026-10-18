// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDOUT: call to 'strtok_r' argument 2 \[0\]: Write of 1 byte\(s\) at .* is outside VM memory
// CCCC_EXPECT_STDERR: compile-time macro 'probe' aborted
#include[[cccc::comptime]] < stdlib.h>
#include[[cccc::comptime]] < string.h>

[[cccc::comptime]]
Node *probe(void) {
    char *save = (char *)8;
    strtok_r(NULL, ",", &save);
    return __builtin_ast_int_literal(1);
}

int main(void) {
    return probe();
}
