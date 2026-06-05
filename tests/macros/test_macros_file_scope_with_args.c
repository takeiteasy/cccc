// Test ticket #275: file-scope macro call with a string literal argument.
// A non-inline macro that receives a char* name generates a function with
// that name; the call happens before the main parse.

[[jcc::macro]]
void make_named_func(char *name) {
    _VirtualMachine *vm = __jcc_get_vm();
    _Type *int_ty = __jcc_ast_get_type(vm, "int");
    _Obj *fn = __jcc_ast_function(vm, name, int_ty);
    _Node *body = __jcc_ast_return(vm, __jcc_ast_int_literal(vm, 42));
    __jcc_ast_function_set_body(vm, fn, body);
}

make_named_func("genned");

int main(void) {
    return genned();
}
