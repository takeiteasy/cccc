// A node may be shared inside a DAG and reused across separate calls.
[[cccc::comptime]]
Node *probe(void) {
    Node *a     = MakeIntLiteral(21);
    Node *sum   = __builtin_ast_binary(NK_ADD, a, a);
    Node *s1    = MakeExprStmt(MakeIntLiteral(1));
    Node *s2    = MakeExprStmt(MakeIntLiteral(2));
    Node *x[2]  = {s1, s2};
    Node *y[2]  = {s2, s1};
    __builtin_ast_block(x, 2);
    __builtin_ast_block(y, 2);
    Type *s     = __builtin_ast_make_struct("Node2");
    __builtin_ast_struct_add_field(s, "next", __builtin_ast_make_pointer(s));
    return sum;
}

int main(void) {
    return probe();
}
