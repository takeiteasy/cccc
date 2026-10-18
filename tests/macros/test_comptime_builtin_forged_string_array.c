// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDOUT: call to '__builtin_ast_offsetof_chain'
// CCCC_EXPECT_STDERR: compile-time macro 'probe' aborted
[[cccc::comptime]]
Node *probe(void) {
    const char *names[1] = {(const char *)8};
    Type       *ty       = GetType("int");
    return MakeIntLiteral((int)__builtin_ast_offsetof_chain(ty, names, 1));
}

int main(void) {
    return probe();
}
