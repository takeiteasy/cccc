// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: static local
[[cccc::comptime]]
Node *probe(void) {
    Node *s = Quote("{ static int n; n++; }");
    return CopyNode(s);
}

int main(void) {
    probe();
    return 0;
}
