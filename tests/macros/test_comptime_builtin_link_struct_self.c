// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: struct would contain itself
// A builtin that would link a node into a cycle is a compile error.
[[cccc::comptime]]
Node *probe(void) {
    Type *s = __builtin_ast_make_struct("Self");
    __builtin_ast_struct_add_field(s, "x", s);
    return MakeIntLiteral(0);
}

int main(void) {
    probe();
    return 0;
}
