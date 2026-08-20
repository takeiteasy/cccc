// Ticket #1034 (cluster A): a local created via __builtin_ast_local_var_unique
// (parse_core.c's new_unique_name(), the same ".L..N" dotted scheme an
// anonymous *global* uses -- rename_anon_globals()) was never renamed to a
// legal C identifier before -c=native printed it, at either its declaration
// or any of its ND_VAR uses -- e.g. `int .L..29;`. src/serialize.c's local
// hoist loop already renamed the empty-name (`__cccc_tmp%d`) case; this
// covers the dotted case the same way, reusing anon_local_counter.
//
// Minimized from test_macros_local_var.c's `doubled()` helper -- narrowed
// to just the unique-local shape, no WithFn/named-local variants needed to
// reproduce cluster A.

[[cccc::comptime]]
Node *doubled(Node *arg) {
    Type *ty_int = __builtin_ast_get_type("int");
    Node *tmp    = __builtin_ast_local_var_unique(ty_int);
    Node *two    = __builtin_ast_int_literal(2);
    Node *mul    = __builtin_ast_binary(NK_MUL, arg, two);
    return __builtin_ast_assign(tmp, mul);
}

int main(void) {
    int r1 = doubled(7); // tmp = 7*2 = 14
    if (r1 != 14)
        return 1;
    int r2 =
        doubled(20); // a second dotted temp -- must not collide with the first
    if (r2 != 40)
        return 2;
    return 42;
}
