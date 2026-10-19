// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers
// A copied checked-array access keeps its own bounds check.
int a _Checked[4];
int i;

[[cccc::comptime]]
Node *probe(void) {
    Node *s = Quote("a[i] = 1;");
    return CopyNode(s);
}

int main(void) {
    volatile int n = 4;
    i              = n;
    probe();
    return 42;
}
