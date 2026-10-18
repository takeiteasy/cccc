// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDOUT: call to '__builtin_macro_error_at' argument 2
// CCCC_EXPECT_STDERR: compile-time macro 'probe' aborted
[[cccc::comptime]]
Node *probe(void) {
    MacroErrorAt(MakeIntLiteral(1), "%s", (char *)8);
    return MakeIntLiteral(42);
}

int main(void) {
    return probe();
}
