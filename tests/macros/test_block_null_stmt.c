// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: MakeBlock: statement 2 is NULL
// A NULL statement among the first count is an error, not the end of the block.
[[cccc::comptime]]
Node *probe(void) {
    Node *stmts[2] = {MakeReturn(MakeIntLiteral(1)), NULL};
    return MakeBlock(stmts, 2);
}

int main(void) {
    probe();
    return 0;
}
