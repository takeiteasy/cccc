// A copied VLA inside a loop is not reclaimed while its block runs.
int n = 3, g;

[[cccc::comptime]]
Node *probe(void) {
    Node *s    = Quote("for (int r = 0; r < 3; r++) { int a[n]; a[0] = r;"
                       " int *q = a; g += q[0] * 7; }");
    Node *x[2] = {s, CopyNode(s)};
    return __builtin_ast_block(x, 2);
}

int main(void) {
    probe();
    return g == 42 ? 42 : 10;
}
