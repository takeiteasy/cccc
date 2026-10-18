// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDOUT: call to '__builtin_ast_token_from_node' argument 0: not a Node handle
// CCCC_EXPECT_STDERR: compile-time macro 'probe' aborted
[[cccc::comptime]]
Node *probe(void) {
    Token *t = TokenFromNode((Node *)8);
    return __builtin_ast_int_literal(t ? 1 : 42);
}

int main(void) {
    return probe();
}
