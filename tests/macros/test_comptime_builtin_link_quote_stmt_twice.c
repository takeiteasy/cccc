// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: quote placeholder repeated in one list
// A builtin that would link a node into a cycle is a compile error.
[[cccc::comptime]]
Node *probe(void) {
    Node *s = MakeExprStmt(MakeIntLiteral(1));
    return Quote("{ $1; $1; }", s);
}

int main(void) {
    probe();
    return 0;
}
