// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: CopyNode cannot copy
// CopyNode refuses a statement that carries a user label.
[[cccc::comptime]]
Node *probe(void) {
    Node *s = Quote("lbl: ;");
    return CopyNode(s);
}

int main(void) {
    probe();
    return 0;
}
