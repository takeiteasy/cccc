// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: NodeList: node 2 is NULL
// A NULL node among the first count is an error, not the end of the chain.
[[cccc::comptime]]
Node *probe(void) {
    Node *nodes[2] = {MakeIntLiteral(1), NULL};
    return NodeList(nodes, 2);
}

int main(void) {
    probe();
    return 0;
}
