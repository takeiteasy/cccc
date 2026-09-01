// A named `goto` whose label is defined in the same Quote() template must
// emit a real jump. Before, codegen's ND_GOTO arm was `if (node->unique_label)
// { emit JMP }` with no else, and a template goto never had unique_label set,
// so it silently fell through.

[[cccc::comptime]]
Node *g(Node *unused) {
    return Quote("{ goto done; return 1; done: return 42; }");
}

int test(void) {
    g(0);
}

int main(void) {
    return test();
}
