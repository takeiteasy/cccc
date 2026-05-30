// Test: Generate a function with parameters at compile-time
#include <reflection.h>

// Forward declare the function we'll generate
int add_numbers(int a, int b);

#pragma macro
_Node *gen_add_func(void) {
    _VirtualMachine *vm = __jcc_get_vm();

    _Type *int_type = __jcc_ast_get_type(vm, "int");
    _Obj *fn = __jcc_ast_function(vm, "add_numbers", int_type);

    // Add parameters
    __jcc_ast_function_add_param(vm, fn, "a", int_type);
    __jcc_ast_function_add_param(vm, fn, "b", int_type);

    // Body: return a + b;
    _Node *a_ref = __jcc_ast_param_ref(vm, fn, "a");
    _Node *b_ref = __jcc_ast_param_ref(vm, fn, "b");
    _Node *sum = __jcc_ast_binary(vm, _ADD, a_ref, b_ref);
    _Node *body = __jcc_ast_return(vm, sum);
    __jcc_ast_function_set_body(vm, fn, body);

    return __jcc_ast_int_literal(vm, 0);
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
