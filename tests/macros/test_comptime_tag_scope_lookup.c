// Tags declared by comptime calls in one function stay findable.
[[cccc::comptime]]
Node *first(void) {
    Type *s = __builtin_ast_make_struct("TagA");
    __builtin_ast_struct_add_field(s, "v", GetType("int"));
    return MakeIntLiteral(FindType("TagA") == s);
}

[[cccc::comptime]]
Node *second(void) {
    Type *s = __builtin_ast_make_struct("TagB");
    __builtin_ast_struct_add_field(s, "v", GetType("int"));
    return MakeIntLiteral(FindType("TagB") == s && FindType("TagA") != NULL);
}

int main(void) {
    return first() && second() ? 42 : 1;
}
