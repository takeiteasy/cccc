// A node may be shared inside an expression DAG and a struct may point to itself.
[[cccc::comptime]]
Node *probe(void) {
    Node *a     = MakeIntLiteral(21);
    Node *sum   = __builtin_ast_binary(NK_ADD, a, a);
    Type *s     = __builtin_ast_make_struct("Node2");
    __builtin_ast_struct_add_field(s, "next", __builtin_ast_make_pointer(s));
    return sum;
}

int main(void) {
    return probe();
}
