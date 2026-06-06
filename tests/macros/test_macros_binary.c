// Test pragma macro with binary expression generation

// Define a pragma macro that generates: (a + b) * 2
[[jcc::macro(inline)]]
$node_t *double_sum($node_t *a, $node_t *b) {
    $vm_t *vm = __jcc_get_vm();
    $node_t *sum = __jcc_ast_binary(vm, nk_add, a, b);
    $node_t *two = __jcc_ast_int_literal(vm, 2);
    return __jcc_ast_binary(vm, nk_mul, sum, two);
}

int main(void) {
    // This should expand to: (3 + 4) * 2 = 14
    int result = double_sum(3, 4);

    if (result != 14) {
        return 1;
    }

    // Test with different values: (10 + 5) * 2 = 30
    int result2 = double_sum(10, 5);
    if (result2 != 30) {
        return 2;
    }

    return 42;
}
