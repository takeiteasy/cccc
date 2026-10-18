// CopyNode lets one Quote statement sit in two Quote blocks.
int g;

[[cccc::comptime]]
Node *probe(void) {
    Node *a    = Quote("g += 1;");
    Node *x    = Quote("{ $1; g += 10; }", a);
    Node *y    = Quote("{ $1; g += 100; }", CopyNode(a));
    Node *s[2] = {x, y};
    return __builtin_ast_block(s, 2);
}

int main(void) {
    probe();
    return g == 112 ? 42 : 1;
}
