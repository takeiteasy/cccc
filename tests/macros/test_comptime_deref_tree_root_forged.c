// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDOUT: call to 'tfind' argument 1: the tree root is not a node libc returned
// CCCC_EXPECT_STDERR: compile-time macro 'probe' aborted
#include[[cccc::comptime]] < search.h>
#include[[cccc::comptime]] < stdlib.h>
#include[[cccc::comptime]] < string.h>

[[cccc::comptime]] static int cmp_int(const void *a, const void *b) {
    return *(const int *)a - *(const int *)b;
}

[[cccc::comptime]]
Node *probe(void) {
    int   key  = 1;
    void *root = (void *)0x1000;
    tfind(&key, &root, cmp_int);
    return __builtin_ast_int_literal(1);
}

int main(void) {
    return probe();
}
