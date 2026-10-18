// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: already in another list
// A node in one NodeList chain cannot join another.
[[cccc::comptime]]
Node *probe(void) {
    Node *a    = MakeIntLiteral(1);
    Node *b    = MakeIntLiteral(2);
    Node *x[2] = {a, b};
    Node *y[2] = {b, a};
    NodeList(x, 2);
    return NodeList(y, 2);
}

int main(void) {
    probe();
    return 0;
}
