// Test: global macro call runs before the main parse (ticket #229)
//
// A file-scope call to a non-inline macro:
//   - Executes before the main parse begins
//   - Generates definitions visible to the whole program
//   - No explicit forward declaration needed

[[cccc::comptime]]
void generate_const_func(void) {
    VirtualMachine *vm = __builtin_get_vm();

    Type *int_type = __builtin_ast_get_type(vm, "int");
    Obj *fn = __builtin_ast_function(vm, "generated_func", int_type);

    Node *ret_val = __builtin_ast_int_literal(vm, 42);
    Node *ret_stmt = __builtin_ast_return(vm, ret_val);
    __builtin_ast_function_set_body(vm, fn, ret_stmt);
}

generate_const_func();

int main(void) {
    int result = generated_func();
    return result;  // 0 on success
}
