// A builtin called through a function pointer still works with valid arguments.
[[cccc::comptime]]
Node *probe(void) {
    Type *(*find)(const char *) = __builtin_ast_get_type;
    Type *t                     = find("int");
    return MakeIntLiteral(t ? 42 : 1);
}

int main(void) {
    return probe();
}
