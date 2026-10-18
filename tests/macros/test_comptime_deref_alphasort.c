// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDOUT: call to 'alphasort' argument 0 \[0\]: Read of .* is outside VM memory
// CCCC_EXPECT_STDERR: compile-time macro 'probe' aborted
#include[[cccc::comptime]] < dirent.h>
#include[[cccc::comptime]] < stdlib.h>
#include[[cccc::comptime]] < string.h>

[[cccc::comptime]]
Node *probe(void) {
    const struct dirent *p1 = (const struct dirent *)8, *p2 = p1;
    alphasort(&p1, &p2);
    return __builtin_ast_int_literal(1);
}

int main(void) {
    return probe();
}
