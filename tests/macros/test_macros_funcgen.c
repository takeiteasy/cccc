// Test pragma macro function generation
// A macro that generates a simple function at compile time

// Pragma macro that generates a function returning a constant
[[cccc::comptime]]
Node *generate_const_func(Node *name_node, Node *value_node) {

    // Create a function: int generated_func(void) { return 42; }
    Type *int_type = __builtin_ast_get_type("int");
    Obj *fn = __builtin_ast_function("generated_func", int_type);
    
    // Set the body to return 42
    Node *ret_val = __builtin_ast_int_literal(42);
    Node *ret_stmt = __builtin_ast_return(ret_val);
    __builtin_ast_function_set_body(fn, ret_stmt);
    
    // Return a placeholder (the function generation is a side effect)
    return __builtin_ast_int_literal(0);
}

// Forward declare the function that will be generated
int generated_func(void);

int main(void) {
    // This call triggers the macro, which generates generated_func
    int dummy = generate_const_func(0, 0);
    (void)dummy;
    
    // Now call the generated function
    int result = generated_func();
    
    if (result != 42) {
        return 1;
    }
    
    return 42;
}
