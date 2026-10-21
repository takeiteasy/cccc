// -c=native, a local created via __builtin_ast_local_var_unique
// (new_unique_name()'s dotted ".L..N" scheme, the same one an anonymous global
// uses) used to serialize its dotted name verbatim at both its declaration and
// every reference (e.g. `int .L..29;`) -- not a legal C identifier. The
// local-hoist loop's existing empty-name rename now covers the dotted case too.
// Asserts VM 42 -> native 42

[[cccc::comptime]]
Node *doubled(Node *arg) {
    Type *ty_int = __builtin_ast_get_type("int");
    Node *tmp    = __builtin_ast_local_var_unique(ty_int);
    Node *two    = __builtin_ast_int_literal(2);
    Node *mul    = __builtin_ast_binary(NK_MUL, arg, two);
    return __builtin_ast_assign(tmp, mul);
}

int main(void) {
    int r1 = doubled(7);
    if (r1 != 14)
        return 1;
    int r2 = doubled(20);
    if (r2 != 40)
        return 2;
    return 42;
}
