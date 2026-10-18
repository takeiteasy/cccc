// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDOUT: call to '__builtin_ast_find_type' argument 0
// CCCC_EXPECT_STDERR: compile-time macro 'probe' aborted
[[cccc::comptime]]
Node *probe(void) {
    Type *t = FindType((const char *)8);
    return MakeIntLiteral(t ? 1 : 42);
}

int main(void) {
    return probe();
}
