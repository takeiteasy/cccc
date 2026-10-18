// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDOUT: call to 'hsearch' argument 0 \[0\]: Read of 1 byte\(s\) at .* is outside VM memory
// CCCC_EXPECT_STDERR: compile-time macro 'probe' aborted
#include[[cccc::comptime]] < search.h>
#include[[cccc::comptime]] < stdlib.h>
#include[[cccc::comptime]] < string.h>

[[cccc::comptime]]
Node *probe(void) {
    hcreate(4);
    ENTRY it = {(char *)8, 0};
    hsearch(it, FIND);
    return __builtin_ast_int_literal(1);
}

int main(void) {
    return probe();
}
