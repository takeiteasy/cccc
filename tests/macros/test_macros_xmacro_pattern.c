// Test ticket #275: X-macro pattern — a C preprocessor macro drives multiple
// file-scope CCCC macro calls, each with a stringified token argument.
// Each expansion generates a function named after the token.

#define ITEMS X(foo) X(bar) X(baz)

[[cccc::comptime]]
void make_fn(char *name) {
    VirtualMachine *vm = __builtin_get_vm();
    Type *int_ty = __builtin_ast_get_type(vm, "int");
    Obj *fn = __builtin_ast_function(vm, name, int_ty);
    Node *body = __builtin_ast_return(vm, __builtin_ast_int_literal(vm, 42));
    __builtin_ast_function_set_body(vm, fn, body);
}

#define X(N) make_fn(#N);
ITEMS
#undef X

int main(void) {
    if (foo() != 42) return 1;
    if (bar() != 42) return 2;
    if (baz() != 42) return 3;
    return 42;
}
