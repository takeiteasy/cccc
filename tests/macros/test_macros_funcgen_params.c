// Test: Generate a function with parameters at compile-time
// Forward declare the function we'll generate
int add_numbers(int a, int b);

[[jcc::macro(inline)]]
$node_t *gen_add_func(void) {
    $vm_t *vm = __jcc_get_vm();

    $type_t *int_type = __jcc_ast_get_type(vm, "int");
    $obj_t *fn = __jcc_ast_function(vm, "add_numbers", int_type);

    // Add parameters
    __jcc_ast_function_add_param(vm, fn, "a", int_type);
    __jcc_ast_function_add_param(vm, fn, "b", int_type);

    // Body: return a + b;
    $node_t *a_ref = __jcc_ast_param_ref(vm, fn, "a");
    $node_t *b_ref = __jcc_ast_param_ref(vm, fn, "b");
    $node_t *sum = __jcc_ast_binary(vm, nk_add, a_ref, b_ref);
    $node_t *body = __jcc_ast_return(vm, sum);
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
