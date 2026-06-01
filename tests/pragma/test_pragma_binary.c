// Test pragma macro with binary expression generation

// Define a pragma macro that generates: (a + b) * 2
#pragma macro
_Node *double_sum(_Node *a, _Node *b) {
    _VirtualMachine *vm = __jcc_get_vm();
    _Node *sum = __jcc_ast_binary(vm, _ADD, a, b);
    _Node *two = __jcc_ast_int_literal(vm, 2);
    return __jcc_ast_binary(vm, _MUL, sum, two);
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
