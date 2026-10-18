// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDOUT: %n is not allowed at comptime
// CCCC_EXPECT_STDERR: compile-time macro 'probe' aborted
[[cccc::comptime]]
Node *probe(void) {
    int n = 0;
    MacroWarningAt(MakeIntLiteral(1), "%n", &n);
    return MakeIntLiteral(42);
}

int main(void) {
    return probe();
}
