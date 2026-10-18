// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDOUT: call to '__builtin_ast_type_kind' argument 0: not a Type handle
// CCCC_EXPECT_STDERR: compile-time macro 'probe' aborted
[[cccc::comptime]]
Node *probe(void) {
    Node *n = MakeIntLiteral(1);
    return MakeIntLiteral(GetTypeKind((Type *)n));
}

int main(void) {
    return probe();
}
