// A copy that only reads an enclosing VLA keeps its size.
int g;

[[cccc::comptime]]
Node *probe(void) {
    Node *s    = Quote("g += m[1][2];");
    Node *x[2] = {s, CopyNode(s)};
    return __builtin_ast_block(x, 2);
}

int main(void) {
    int n = 3, k = 4;
    int m[n][k];
    for (int i = 0; i < n; i++)
        for (int j = 0; j < k; j++)
            m[i][j] = i * 10 + j;
    probe();
    return g == 24 ? 42 : 10;
}
