// A copied switch dispatches to its own cases, range, default and fallthrough.
int g, v;

[[cccc::comptime]]
Node *probe(void) {
    Node *s = Quote("switch (v) { case 1: g += 5; case 2 ... 4: g += 1; break;"
                    " default: g += 100; }");
    Node *x[3] = {s, CopyNode(s), CopyNode(s)};
    return __builtin_ast_block(x, 3);
}

int main(void) {
    v = 1;
    probe(); // 6 three times
    return g == 18 ? 42 : 1;
}
