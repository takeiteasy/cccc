// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDOUT: call to 'regexec' argument 0: not a regex_t compiled by regcomp
// CCCC_EXPECT_STDERR: compile-time macro 'probe' aborted
#include[[cccc::comptime]] < regex.h>
#include[[cccc::comptime]] < stdlib.h>
#include[[cccc::comptime]] < string.h>

[[cccc::comptime]]
Node *probe(void) {
    regex_t re;
    memset(&re, 0, sizeof re);
    regexec(&re, "a", 0, NULL, 0);
    return __builtin_ast_int_literal(1);
}

int main(void) {
    return probe();
}
