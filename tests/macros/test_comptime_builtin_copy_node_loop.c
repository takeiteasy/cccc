// A copied loop gets its own break/continue targets.
int g, i;

[[cccc::comptime]]
Node *probe(void) {
    Node *loop = Quote("for (i = 0; i < 10; i++) { if (i == 3) break; g += 7; }");
    Node *s[2] = {loop, CopyNode(loop)};
    return __builtin_ast_block(s, 2);
}

int main(void) {
    probe();
    return g == 42 ? 42 : 1;
}
