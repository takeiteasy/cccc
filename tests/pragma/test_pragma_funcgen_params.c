// Test: Generate a function with parameters at compile-time
#include <reflection.h>

// Forward declare the function we'll generate
int add_numbers(int a, int b);

#pragma macro
JCC_Node *gen_add_func(void) {
    JCC *vm = jcc_get_vm();

    JCC_Type *int_type = jcc_ast_get_type(vm, "int");
    JCC_Obj *fn = jcc_ast_function(vm, "add_numbers", int_type);

    // Add parameters
    jcc_ast_function_add_param(vm, fn, "a", int_type);
    jcc_ast_function_add_param(vm, fn, "b", int_type);

    // Body: return a + b;
    JCC_Node *a_ref = jcc_ast_param_ref(vm, fn, "a");
    JCC_Node *b_ref = jcc_ast_param_ref(vm, fn, "b");
    JCC_Node *sum = jcc_ast_binary(vm, JCC_ND_ADD, a_ref, b_ref);
    JCC_Node *body = jcc_ast_return(vm, sum);
    jcc_ast_function_set_body(vm, fn, body);

    return jcc_ast_int_literal(vm, 0);
}

int main(void) {
    gen_add_func(); // Generate the function at compile-time

    int result = add_numbers(20, 22);
    if (result != 42)
        return 1;

    result = add_numbers(100, -58);
    if (result != 42)
        return 2;

    result = add_numbers(0, 0);
    if (result != 0)
        return 3;

    return 42;
}
