// CopyNode lets one statement sit in two blocks; the first keeps both.
int g;

[[cccc::comptime]]
Node *probe(void) {
    Node *a    = Quote("g += 20;");
    Node *b    = Quote("g += 22;");
    Node *x[2] = {a, b};
    Node *y[2] = {CopyNode(b), CopyNode(a)};
    Node *s[2] = {__builtin_ast_block(x, 2), __builtin_ast_block(y, 2)};
    return __builtin_ast_block(s, 2);
}

int main(void) {
    probe();
    return g == 84 ? 42 : 1;
}
