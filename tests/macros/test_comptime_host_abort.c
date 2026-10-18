// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: terminated by host signal [0-9]+ in 'abort'
// CCCC_REJECT_STDERR: error(s)? generated
#include[[cccc::comptime]] < stdlib.h>

[[cccc::comptime]]
Node *probe(void) {
    abort();
    return __builtin_ast_int_literal(42);
}

int main(void) {
    return probe();
}
