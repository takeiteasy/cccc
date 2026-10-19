// EXPECT_COMPILE_ERROR CCCC_FLAGS: --checked-pointers
// CCCC_EXPECT_STDERR: checked-pointer declaration
[[cccc::comptime]]
Node *probe(void) {
    Node *s = Quote("{ int a _Checked[4]; a[0] = 1; }");
    return CopyNode(s);
}

int main(void) {
    probe();
    return 0;
}
