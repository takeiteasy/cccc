// EXPECT_COMPILE_ERROR
// Test ticket #286: $@k splice that expands to the wrong number of arguments
// for a fixed-arity callee must produce a compile-time error after expansion.

int add3(int a, int b, int c) {
    return a + b + c;
}

int add2(int a, int b) {
    return a + b;
}

// Too few: splice 2 nodes into a 3-parameter callee.
[[jcc::macro(inline)]]
$node_t *too_few_splice($node_t *a, $node_t *b) {
    $vm_t *vm = __jcc_get_vm();
    $node_t *chain = __jcc_node_list(vm, ($node_t*[]){ a, b }, 2);
    return __jcc_quote(vm, "add3($@1)", chain);
}

int main(void) {
    return too_few_splice(1, 2);
}
