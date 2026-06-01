// Test pragma macro function generation
// A macro that generates a simple function at compile time

// Pragma macro that generates a function returning a constant
#pragma macro
_Node *generate_const_func(_Node *name_node, _Node *value_node) {
    _VirtualMachine *vm = __jcc_get_vm();

    // Create a function: int generated_func(void) { return 42; }
    _Type *int_type = __jcc_ast_get_type(vm, "int");
    _Obj *fn = __jcc_ast_function(vm, "generated_func", int_type);
    
    // Set the body to return 42
    _Node *ret_val = __jcc_ast_int_literal(vm, 42);
    _Node *ret_stmt = __jcc_ast_return(vm, ret_val);
    __jcc_ast_function_set_body(vm, fn, ret_stmt);
    
    // Return a placeholder (the function generation is a side effect)
    return __jcc_ast_int_literal(vm, 0);
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
