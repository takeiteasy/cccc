// EXPECT_COMPILE_ERROR
// Test ticket #78: __jcc_macro_error_at emits a source-located error and fails compilation.

// Macro that always errors with a located message.
[[jcc::macro(inline)]]
_Node *always_error(_Node *n) {
    _VirtualMachine *vm = __jcc_get_vm();
    __jcc_macro_error_at(vm, n, "always_error: this argument is not allowed");
    return n; // unreachable
}

int main(void) {
    int x = always_error(42);  // compiler error should point here
    return x;
}
