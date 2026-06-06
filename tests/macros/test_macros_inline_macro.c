// Test: global macro call runs before the main parse (ticket #229)
//
// A file-scope call to a non-inline macro:
//   - Executes before the main parse begins
//   - Generates definitions visible to the whole program
//   - No explicit forward declaration needed

[[jcc::macro]]
void generate_const_func(void) {
    $vm_t *vm = __jcc_get_vm();

    $type_t *int_type = __jcc_ast_get_type(vm, "int");
    $obj_t *fn = __jcc_ast_function(vm, "generated_func", int_type);

    $node_t *ret_val = __jcc_ast_int_literal(vm, 42);
    $node_t *ret_stmt = __jcc_ast_return(vm, ret_val);
    __jcc_ast_function_set_body(vm, fn, ret_stmt);
}

generate_const_func();

int main(void) {
    int result = generated_func();
    return result;  // 0 on success
}
