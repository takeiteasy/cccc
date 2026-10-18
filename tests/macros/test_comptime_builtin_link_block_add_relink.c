// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: already in another list
// BlockAddStmt would cut the other block's later statements off.
[[cccc::comptime]]
Node *probe(void) {
    Node *a    = MakeExprStmt(MakeIntLiteral(1));
    Node *b    = MakeExprStmt(MakeIntLiteral(2));
    Node *x[2] = {a, b};
    __builtin_ast_block(x, 2);
    Node *other = __builtin_ast_block(0, 0);
    __builtin_ast_block_add_stmt(other, a);
    return other;
}

int main(void) {
    probe();
    return 0;
}
