// EXPECT_COMPILE_ERROR
// Test ticket #78: __cccc_macro_error_at emits a source-located error and fails compilation.

// Macro that always errors with a located message.
[[cccc::comptime(inline)]]
$node_t *always_error($node_t *n) {
    $vm_t *vm = __cccc_get_vm();
    __cccc_macro_error_at(vm, n, "always_error: this argument is not allowed");
    return n; // unreachable
}

int main(void) {
    int x = always_error(42);  // compiler error should point here
    return x;
}
