// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDOUT: call to '__builtin_node_list' argument 0: not a Node handle
// CCCC_EXPECT_STDERR: compile-time macro 'probe' aborted
[[cccc::comptime]]
Node *probe(void) {
    Node *items[2] = {MakeIntLiteral(1), (Node *)8};
    return __builtin_node_list(items, 2);
}

int main(void) {
    return probe();
}
