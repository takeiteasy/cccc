// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDOUT: call to 'insque' argument 1 \[0\]: Write of 16 byte\(s\) at .* is outside VM memory
// CCCC_EXPECT_STDERR: compile-time macro 'probe' aborted
#include[[cccc::comptime]] < search.h>
#include[[cccc::comptime]] < stdlib.h>
#include[[cccc::comptime]] < string.h>

[[cccc::comptime]]
Node *probe(void) {
    struct Q {
        struct Q *next, *prev;
    } a = {(struct Q *)8, 0}, b = {0, 0};
    insque(&b, &a);
    return __builtin_ast_int_literal(1);
}

int main(void) {
    return probe();
}
