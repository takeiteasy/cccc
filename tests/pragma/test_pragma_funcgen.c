// Test pragma macro function generation
// A macro that generates a simple function at compile time

#include <reflection.h>

// Pragma macro that generates a function returning a constant
#pragma macro
JCC_Node *generate_const_func(JCC_Node *name_node, JCC_Node *value_node) {
    JCC *vm = jcc_get_vm();

    // Create a function: int generated_func(void) { return 42; }
    JCC_Type *int_type = jcc_ast_get_type(vm, "int");
    JCC_Obj *fn = jcc_ast_function(vm, "generated_func", int_type);
    
    // Set the body to return 42
    JCC_Node *ret_val = jcc_ast_int_literal(vm, 42);
    JCC_Node *ret_stmt = jcc_ast_return(vm, ret_val);
    jcc_ast_function_set_body(vm, fn, ret_stmt);
    
    // Return a placeholder (the function generation is a side effect)
    return jcc_ast_int_literal(vm, 0);
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
