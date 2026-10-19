// &&label and goto * in a copy target the copy's own label.
int g;

[[cccc::comptime]]
Node *probe(void) {
    Node *s = Quote("{ void *p = &&out; goto *p; g += 100; out: g += 21; }");
    Node *x[2] = {s, CopyNode(s)};
    return __builtin_ast_block(x, 2);
}

int main(void) {
    probe();
    return g == 42 ? 42 : 1;
}
