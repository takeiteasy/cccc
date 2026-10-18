// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDOUT: call to 'qsort' argument 3: function pointer is NULL
// CCCC_EXPECT_STDERR: compile-time macro 'probe' aborted
#include[[cccc::comptime]] < stdlib.h>
#include[[cccc::comptime]] < string.h>

[[cccc::comptime]]
Node *probe(void) {
    int nums[2] = {2, 1};
    qsort(nums, 2, sizeof nums[0], NULL);
    return __builtin_ast_int_literal(1);
}

int main(void) {
    return probe();
}
