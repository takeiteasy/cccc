// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDOUT: call to '__builtin_ast_global_var_set_init_data' argument 1
// CCCC_EXPECT_STDERR: compile-time macro 'probe' aborted
[[cccc::comptime]]
Node *probe(void) {
    Obj *v = __builtin_ast_global_var("probe_buf", GetType("int"));
    __builtin_ast_global_var_set_init_data(v, (const char *)8, 4);
    return MakeIntLiteral(42);
}

int main(void) {
    return probe();
}
