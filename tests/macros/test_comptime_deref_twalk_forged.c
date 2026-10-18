// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDOUT: call to 'twalk' argument 0: not a tree node libc returned
// CCCC_EXPECT_STDERR: compile-time macro 'probe' aborted
#include[[cccc::comptime]] < search.h>
#include[[cccc::comptime]] < stdlib.h>
#include[[cccc::comptime]] < string.h>

[[cccc::comptime]] static void visit(const void *node, VISIT which, int depth) {
    (void)node, (void)which, (void)depth;
}

[[cccc::comptime]]
Node *probe(void) {
    twalk((void *)8, visit);
    return __builtin_ast_int_literal(1);
}

int main(void) {
    return probe();
}
