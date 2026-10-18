// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --comptime-allow=fs
// CCCC_EXPECT_STDOUT: free: Write of 1 byte\(s\) at .* is outside VM memory
// CCCC_EXPECT_STDERR: compile-time macro 'probe' aborted
#include <glob.h>
#include <stdlib.h>

[[cccc::comptime]]
Node *probe(void) {
    glob_t g;
    glob("/dev/null", 0, NULL, &g);
    free(g.gl_pathv[0]);
    return __builtin_ast_int_literal(1);
}

int main(void) {
    return probe();
}
