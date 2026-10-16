// Comptime macro calls are expanded where the parser needs a constant:
// enumerator values, file-scope array lengths and _Static_assert. A call used
// as a block-scope array length stays a runtime (VLA) length.

[[cccc::comptime]]
Node *PLUS(Node *a, Node *b) {
    return MakeBinary(NK_ADD, a, b);
}

[[cccc::comptime]]
Node *FOUR(void) {
    return MakeIntLiteral(4);
}

enum { A = FOUR(), B = PLUS(A, 1), C };

int table[PLUS(2, 3)];

_Static_assert(PLUS(40, 2) == 42, "macro call in a static assertion");

static int vla_sum(int n) {
    int v[PLUS(n, 1)];
    for (int i = 0; i < n + 1; i++)
        v[i] = i;
    int s = 0;
    for (int i = 0; i < n + 1; i++)
        s += v[i];
    return s;
}

int main(void) {
    if (A != 4 || B != 5 || C != 6)
        return 1;
    if (sizeof(table) != 5 * sizeof(int))
        return 2;
    if (vla_sum(3) != 6)
        return 3;
    return 42;
}
