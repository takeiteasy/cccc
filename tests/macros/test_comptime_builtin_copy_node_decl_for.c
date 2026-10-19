// A copied loop with a declaration in its init clause keeps its own counter.
int g;

[[cccc::comptime]]
Node *probe(void) {
    Node *s    = Quote("for (int i = 0; i < 3; i++) g += 7;");
    Node *x[2] = {s, CopyNode(s)};
    return __builtin_ast_block(x, 2);
}

int main(void) {
    probe();
    return g == 42 ? 42 : 1;
}
