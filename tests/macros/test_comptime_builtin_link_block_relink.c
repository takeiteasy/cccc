// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: already in another list
// A node linked into one block cannot be linked into a second.
[[cccc::comptime]]
Node *probe(void) {
    Node *a    = MakeExprStmt(MakeIntLiteral(1));
    Node *b    = MakeExprStmt(MakeIntLiteral(2));
    Node *x[2] = {a, b};
    Node *y[2] = {b, a};
    __builtin_ast_block(x, 2);
    return __builtin_ast_block(y, 2);
}

int main(void) {
    probe();
    return 0;
}
