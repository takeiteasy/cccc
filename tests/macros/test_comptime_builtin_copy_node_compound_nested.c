// A copied compound assignment keeps a temporary of its own.
int g;

[[cccc::comptime]]
Node *probe(void) {
    Node *a = Quote("(g += 20)");
    return Quote("(void)($1 + $2 + (g += 2));", a, CopyNode(a));
}

int main(void) {
    probe();
    return g == 42 ? 42 : 1;
}
