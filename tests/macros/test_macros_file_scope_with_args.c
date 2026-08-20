// Test ticket #275: file-scope macro call with a string literal argument.
// A comptime macro that receives a char* name generates a function with
// that name; the call happens before the main parse.

[[cccc::comptime]]
void make_named_func(char *name) {
    Type *int_ty = __builtin_ast_get_type("int");
    Obj  *fn     = __builtin_ast_function(name, int_ty);
    Node *body   = __builtin_ast_return(__builtin_ast_int_literal(42));
    __builtin_ast_function_set_body(fn, body);
}

make_named_func("genned");

int main(void) {
    return genned();
}
