// CCCC_FLAGS: --comptime-allow=fs
// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: compile-time macro 'probe' aborted
#include[[cccc::comptime]] < stdio.h>

[[cccc::comptime]]
Node *probe(void) {
    FILE  *f   = tmpfile();
    size_t cap = 0;
    getline((char **)8, &cap, f);
    return __builtin_ast_int_literal(42);
}

int main(void) {
    return probe();
}
