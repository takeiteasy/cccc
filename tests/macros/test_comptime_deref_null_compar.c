// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDOUT: call to 'tsearch' argument 2: function pointer is NULL
// CCCC_EXPECT_STDERR: compile-time macro 'probe' aborted
#include[[cccc::comptime]] < search.h>
#include[[cccc::comptime]] < stdlib.h>
#include[[cccc::comptime]] < string.h>

[[cccc::comptime]]
Node *probe(void) {
    int   key  = 1;
    void *root = NULL;
    tsearch(&key, &root, NULL);
    return __builtin_ast_int_literal(1);
}

int main(void) {
    return probe();
}
