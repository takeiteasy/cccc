// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDOUT: call to 'regfree' argument 0: not a regex_t compiled by regcomp
// CCCC_EXPECT_STDERR: compile-time macro 'probe' aborted
#include[[cccc::comptime]] < regex.h>
#include[[cccc::comptime]] < stdlib.h>
#include[[cccc::comptime]] < string.h>

[[cccc::comptime]]
Node *probe(void) {
    regex_t re;
    regcomp(&re, "a", REG_EXTENDED);
    re.re_nsub = 99;
    regfree(&re);
    return __builtin_ast_int_literal(1);
}

int main(void) {
    return probe();
}
