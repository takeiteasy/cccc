// EXPECT_COMPILE_ERROR
// Test ticket #194: using $@N as a sub-expression within a call argument
// (rather than as a direct top-level argument) must produce a compile-time error.

#include "stdarg.h"

int sum_ints(int count, ...) { return 0; }

[[jcc::comptime(inline)]]
$node_t *bad_arg_splice($node_t *x) {
    $vm_t *vm = __jcc_get_vm();
    // $@1 cannot be used as an operand inside an expression; it must be a
    // direct argument, not a sub-expression.
    return __jcc_quote(vm, "sum_ints(1, $@1 + 1)", x);
}

int main(void) {
    int v = 5;
    return bad_arg_splice(v);
}
