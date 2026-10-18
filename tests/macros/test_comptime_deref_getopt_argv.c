// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDOUT: call to 'getopt' argument 1 \[8\]: Read of 1 byte\(s\) at .* is outside VM memory
// CCCC_EXPECT_STDERR: compile-time macro 'probe' aborted
#include[[cccc::comptime]] < getopt.h>
#include[[cccc::comptime]] < stdlib.h>
#include[[cccc::comptime]] < string.h>

[[cccc::comptime]]
Node *probe(void) {
    char *argv[] = {"prog", (char *)8, NULL};
    getopt(2, argv, "a");
    return __builtin_ast_int_literal(1);
}

int main(void) {
    return probe();
}
