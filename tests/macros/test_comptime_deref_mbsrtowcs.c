// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDOUT: call to 'mbsrtowcs' argument 1 \[0\]: Read of 1 byte\(s\) at .* is outside VM memory
// CCCC_EXPECT_STDERR: compile-time macro 'probe' aborted
#include[[cccc::comptime]] < stdlib.h>
#include[[cccc::comptime]] < string.h>
#include[[cccc::comptime]] < wchar.h>

[[cccc::comptime]]
Node *probe(void) {
    const char *src = (const char *)8;
    wchar_t     wide[4];
    mbsrtowcs(wide, &src, 4, NULL);
    return __builtin_ast_int_literal(1);
}

int main(void) {
    return probe();
}
