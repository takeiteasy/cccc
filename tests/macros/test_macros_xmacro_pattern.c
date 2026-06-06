// Test ticket #275: X-macro pattern — a C preprocessor macro drives multiple
// file-scope JCC macro calls, each with a stringified token argument.
// Each expansion generates a function named after the token.

#define ITEMS X(foo) X(bar) X(baz)

[[jcc::comptime]]
void make_fn(char *name) {
    $vm_t *vm = __jcc_get_vm();
    $type_t *int_ty = __jcc_ast_get_type(vm, "int");
    $obj_t *fn = __jcc_ast_function(vm, name, int_ty);
    $node_t *body = __jcc_ast_return(vm, __jcc_ast_int_literal(vm, 42));
    __jcc_ast_function_set_body(vm, fn, body);
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
