// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: node appears twice in one list
// A builtin that would link a node into a cycle is a compile error.
[[cccc::comptime]]
Node *probe(void) {
    Node *a      = MakeExprStmt(MakeIntLiteral(1));
    Node *list[2] = {a, a};
    return __builtin_node_list(list, 2);
}

int main(void) {
    probe();
    return 0;
}
