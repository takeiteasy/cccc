// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: statement is already in this block
// A builtin that would link a node into a cycle is a compile error.
[[cccc::comptime]]
Node *probe(void) {
    Node *s = MakeExprStmt(MakeIntLiteral(1));
    Node *b = __builtin_ast_block(0, 0);
    __builtin_ast_block_add_stmt(b, s);
    __builtin_ast_block_add_stmt(b, s);
    return b;
}

int main(void) {
    probe();
    return 0;
}
