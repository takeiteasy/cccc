// EXPECT_COMPILE_ERROR
// Test ticket #78: jcc_error_at emits a source-located error and fails compilation.

#include <reflection.h>

// Macro that always errors with a located message.
#pragma macro
JCC_Node *always_error(JCC_Node *n) {
    JCC *vm = jcc_get_vm();
    jcc_error_at(vm, n, "always_error: this argument is not allowed");
    return n; // unreachable
}

int main(void) {
    int x = always_error(42);  // compiler error should point here
    return x;
}
