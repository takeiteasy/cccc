// A copied user label gets its own target; the goto in each copy stays inside
// it.
int g;

[[cccc::comptime]]
Node *probe(void) {
    Node *s    = Quote("{ goto out; g += 100; out: g += 21; }");
    Node *x[2] = {s, CopyNode(s)};
    return __builtin_ast_block(x, 2);
}

int main(void) {
    probe();
    return g == 42 ? 42 : 1;
}
