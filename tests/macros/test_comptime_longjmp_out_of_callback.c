// A comptime comparator that longjmps back out of qsort lands in the macro's
// own frame; the macro goes on to expand to 42.
#include[[cccc::comptime]] < setjmp.h>
#include[[cccc::comptime]] < stdlib.h>

[[cccc::comptime]]
static jmp_buf env;

[[cccc::comptime]]
static int cmp(const void *a, const void *b) {
    longjmp(env, 7);
}

[[cccc::comptime]]
Node *probe(void) {
    int nums[3] = {3, 1, 2};
    if (setjmp(env) == 0)
        qsort(nums, 3, sizeof nums[0], cmp);
    return __builtin_ast_int_literal(42);
}

int main(void) {
    return probe();
}
