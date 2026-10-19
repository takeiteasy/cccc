// A copied 2-D VLA and a pointer to its rows keep their own sizes.
int n = 2, k = 3, g;

[[cccc::comptime]]
Node *probe(void) {
    Node *s    = Quote("{ int m[n][k];"
                       " for (int i = 0; i < n; i++)"
                       "   for (int j = 0; j < k; j++) m[i][j] = i * k + j;"
                       " int (*p)[k] = m;"
                       " g += p[1][2] + (int)sizeof(m[0])"
                       "      + (int)(sizeof(m) / sizeof(m[0])) + p[0][2]; }");
    Node *x[2] = {s, CopyNode(s)};
    return __builtin_ast_block(x, 2);
}

int main(void) {
    probe();
    return g == 42 ? 42 : 10;
}
