// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --comptime-allow=fs
// CCCC_EXPECT_STDOUT: outside VM memory
// CCCC_EXPECT_STDERR: compile-time macro 'probe' aborted
#include <glob.h>
#include <string.h>

[[cccc::comptime]]
Node *probe(void) {
    glob_t g;
    glob("/dev/null", 0, NULL, &g);
    char *path = g.gl_pathv[0];
    globfree(&g);
    return __builtin_ast_int_literal((int)strlen(path));
}

int main(void) {
    return probe();
}
