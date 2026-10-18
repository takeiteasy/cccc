// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDOUT: call to '__builtin_quote' argument 2: not a Node handle
// CCCC_EXPECT_STDERR: compile-time macro 'probe' aborted
[[cccc::comptime]]
Node *probe(void) {
    return Quote("$1 + $2", MakeIntLiteral(40), (Node *)8);
}

int main(void) {
    return probe();
}
