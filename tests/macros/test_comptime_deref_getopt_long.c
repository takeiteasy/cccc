// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDOUT: call to 'getopt_long' argument 3 \[0\]: Write of 4 byte\(s\) at .* is outside VM memory
// CCCC_EXPECT_STDERR: compile-time macro 'probe' aborted
#include[[cccc::comptime]] < getopt.h>
#include[[cccc::comptime]] < stdlib.h>
#include[[cccc::comptime]] < string.h>

[[cccc::comptime]]
Node *probe(void) {
    struct option longs[] = {{"verbose", no_argument, (int *)8, 1},
                             {0, 0, 0, 0}};
    char         *argv[]  = {"prog", "--verbose", NULL};
    getopt_long(2, argv, "v", longs, NULL);
    return __builtin_ast_int_literal(1);
}

int main(void) {
    return probe();
}
