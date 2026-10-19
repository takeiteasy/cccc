// A struct-returning call copies with its own return buffer.
typedef struct { int a, b; } Pair;
Pair make(int a, int b) { return (Pair){a, b}; }
int g;

[[cccc::comptime]]
Node *probe(void) {
    Node *s    = Quote("g += make(20, 1).a + make(0, 1).b;");
    Node *x[2] = {s, CopyNode(s)};
    return __builtin_ast_block(x, 2);
}

int main(void) {
    probe();
    return g == 42 ? 42 : 1;
}
