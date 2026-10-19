// A copied VLA has its own size and storage.
int n = 3, g;

[[cccc::comptime]]
Node *probe(void) {
    Node *s = Quote("{ int a[n]; for (int i = 0; i < n; i++) a[i] = 2 * i + 1;"
                    " g += (int)sizeof(a) + a[0] + a[1] + a[2]; }");
    Node *x[2] = {s, CopyNode(s)};
    return __builtin_ast_block(x, 2);
}

int main(void) {
    probe();
    return g == 42 ? 42 : 10;
}
