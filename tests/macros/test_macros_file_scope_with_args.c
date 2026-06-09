// Test ticket #275: file-scope macro call with a string literal argument.
// A non-inline macro that receives a char* name generates a function with
// that name; the call happens before the main parse.

[[cccc::comptime]]
void make_named_func(char *name) {
    $vm_t *vm = __cccc_get_vm();
    $type_t *int_ty = __cccc_ast_get_type(vm, "int");
    $obj_t *fn = __cccc_ast_function(vm, name, int_ty);
    $node_t *body = __cccc_ast_return(vm, __cccc_ast_int_literal(vm, 42));
    __cccc_ast_function_set_body(vm, fn, body);
}

make_named_func("genned");

int main(void) {
    return genned();
}
