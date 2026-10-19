// A copied declaration gives each copy its own variable, initialised or not.
int g;

[[cccc::comptime]]
Node *probe(void) {
    Node *s = Quote("{ int x; int y = 1; x = 9; g += x + y; }");
    Node *x[3] = {s, CopyNode(s), Quote("{ int x = 22; g += x; }")};
    return __builtin_ast_block(x, 3);
}

int main(void) {
    probe();
    return g == 42 ? 42 : 1;
}
