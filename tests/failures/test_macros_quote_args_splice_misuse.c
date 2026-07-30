// EXPECT_COMPILE_ERROR
// Test ticket #194: using $@N as a sub-expression within a call argument
// (rather than as a direct top-level argument) must produce a compile-time error.

#include "stdarg.h"

int sum_ints(int count, ...) { return 0; }

[[cccc::comptime]]
Node *bad_arg_splice(Node *x) {
    // $@1 cannot be used as an operand inside an expression; it must be a
    // direct argument, not a sub-expression.
    return __builtin_quote("sum_ints(1, $@1 + 1)", x);
}

int main(void) {
    int v = 5;
    return bad_arg_splice(v);
}
