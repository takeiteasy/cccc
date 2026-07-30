// Test: Generate a function with parameters at compile-time
// Forward declare the function we'll generate
int add_numbers(int a, int b);

[[cccc::comptime]]
Node *gen_add_func(void) {

    Type *int_type = __builtin_ast_get_type("int");
    Obj *fn = __builtin_ast_function("add_numbers", int_type);

    // Add parameters
    __builtin_ast_function_add_param(fn, "a", int_type);
    __builtin_ast_function_add_param(fn, "b", int_type);

    // Body: return a + b;
    Node *a_ref = __builtin_ast_param_ref(fn, "a");
    Node *b_ref = __builtin_ast_param_ref(fn, "b");
    Node *sum = __builtin_ast_binary(NK_ADD, a_ref, b_ref);
    Node *body = __builtin_ast_return(sum);
    __builtin_ast_function_set_body(fn, body);

    return __builtin_ast_int_literal(0);
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
