// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDOUT: call to '__builtin_ast_node_kind' argument 0: not a Node handle
// CCCC_EXPECT_STDERR: compile-time macro 'probe' aborted
[[cccc::comptime]]
Node *probe(void) {
    NodeKind k = GetNodeKind((Node *)8);
    return __builtin_ast_int_literal(k == NK_NULL_EXPR ? 1 : 42);
}

int main(void) {
    return probe();
}
