// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: variable-length array
int n = 3;

[[cccc::comptime]]
Node *probe(void) {
    Node *s = Quote("{ int a[n]; a[0] = 1; }");
    return CopyNode(s);
}

int main(void) {
    probe();
    return 0;
}
