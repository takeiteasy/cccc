// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDOUT: longjmp target is not a live frame
// CCCC_EXPECT_STDERR: compile-time macro 'probe' aborted
#include <setjmp.h>
#include <string.h>

[[cccc::comptime]]
Node *probe(void) {
    jmp_buf jb;
    memset(&jb, 0, sizeof jb);
    ((long long *)&jb)[1] = 8;
    ((long long *)&jb)[2] = 8;
    longjmp(jb, 1);
    return __builtin_ast_int_literal(1);
}

int main(void) {
    return probe();
}
