// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDOUT: call to 'iconv' argument 1 \[0\]: Read of 2 byte\(s\) at .* is outside VM memory
// CCCC_EXPECT_STDERR: compile-time macro 'probe' aborted
#include[[cccc::comptime]] < iconv.h>
#include[[cccc::comptime]] < stdlib.h>
#include[[cccc::comptime]] < string.h>

[[cccc::comptime]]
Node *probe(void) {
    iconv_t cd = iconv_open("ASCII", "UTF-8");
    char   *ip = (char *)8, out[8], *op = out;
    size_t  il = 2, ol = sizeof out;
    iconv(cd, &ip, &il, &op, &ol);
    return __builtin_ast_int_literal(1);
}

int main(void) {
    return probe();
}
