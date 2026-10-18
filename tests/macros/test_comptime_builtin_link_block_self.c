// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: block would contain itself
// A builtin that would link a node into a cycle is a compile error.
[[cccc::comptime]]
Node *probe(void) {
    Node *b = __builtin_ast_block(0, 0);
    __builtin_ast_block_add_stmt(b, b);
    return b;
}

int main(void) {
    probe();
    return 0;
}
