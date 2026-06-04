// Test: global macro call runs before the main parse (ticket #229)
//
// A file-scope call to a non-inline macro:
//   - Executes before the main parse begins
//   - Generates definitions visible to the whole program
//   - No explicit forward declaration needed

[[jcc::macro]]
void generate_const_func(void) {
    _VirtualMachine *vm = __jcc_get_vm();

    _Type *int_type = __jcc_ast_get_type(vm, "int");
    _Obj *fn = __jcc_ast_function(vm, "generated_func", int_type);

    _Node *ret_val = __jcc_ast_int_literal(vm, 42);
    _Node *ret_stmt = __jcc_ast_return(vm, ret_val);
    __jcc_ast_function_set_body(vm, fn, ret_stmt);
}

generate_const_func();

int main(void) {
    int result = generated_func();
    return result;  // 0 on success
}
