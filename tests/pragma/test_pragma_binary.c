// Test pragma macro with binary expression generation

#include <reflection.h>

// Define a pragma macro that generates: (a + b) * 2
#pragma macro
JCC_Node *double_sum(JCC_Node *a, JCC_Node *b) {
    JCC *vm = jcc_get_vm();
    JCC_Node *sum = jcc_ast_binary(vm, JCC_ND_ADD, a, b);
    JCC_Node *two = jcc_ast_int_literal(vm, 2);
    return jcc_ast_binary(vm, JCC_ND_MUL, sum, two);
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

    return 0;
}
