// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDOUT: call to '__builtin_ast_const_int_value' argument 1
// CCCC_EXPECT_STDERR: compile-time macro 'probe' aborted
[[cccc::comptime]]
Node *probe(void) {
    __builtin_ast_const_int_value(MakeIntLiteral(1), (int64_t *)8);
    return MakeIntLiteral(42);
}

int main(void) {
    return probe();
}
