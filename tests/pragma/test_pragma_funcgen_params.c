// Test: Generate a function with parameters at compile-time
#include <reflection.h>

// Forward declare the function we'll generate
int add_numbers(int a, int b);

#pragma macro
Node *gen_add_func(void) {
    JCC *vm = __jcc_get_vm();

    Type *int_type = ast_get_type(vm, "int");
    Obj *fn = ast_function(vm, "add_numbers", int_type);

    // Add parameters
    ast_function_add_param(vm, fn, "a", int_type);
    ast_function_add_param(vm, fn, "b", int_type);

    // Body: return a + b;
    Node *a_ref = ast_param_ref(vm, fn, "a");
    Node *b_ref = ast_param_ref(vm, fn, "b");
    Node *sum = ast_binary(vm, ND_ADD, a_ref, b_ref);
    Node *body = ast_return(vm, sum);
    ast_function_set_body(vm, fn, body);

    return ast_int_literal(vm, 0);
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

    return 0;
}
