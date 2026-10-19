// A copied atomic compound assignment keeps its own temporaries.
_Atomic int g;

[[cccc::comptime]]
Node *probe(void) {
    Node *s    = Quote("g += 21;");
    Node *x[2] = {s, CopyNode(s)};
    return __builtin_ast_block(x, 2);
}

int main(void) {
    probe();
    return g == 42 ? 42 : 1;
}
