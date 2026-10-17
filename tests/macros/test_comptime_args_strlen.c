// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDOUT: call to 'strlen' argument 0
// CCCC_EXPECT_STDERR: compile-time macro 'probe' aborted
#include <string.h>

[[cccc::comptime]]
Node *probe(void) {
    return __builtin_ast_int_literal((int)strlen((const char *)8));
}

int main(void) {
    return probe();
}
