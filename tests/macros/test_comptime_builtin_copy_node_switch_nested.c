// Nested switches and a break inside the copy leave only the copy.
int g, v;

[[cccc::comptime]]
Node *probe(void) {
    Node *s = Quote("switch (v) { case 3: switch (v + 1) { case 4: g += 20;"
                    " break; default: g += 100; } g += 1; break; }");
    Node *x[2] = {s, CopyNode(s)};
    return __builtin_ast_block(x, 2);
}

int main(void) {
    v = 3;
    probe();
    return g == 42 ? 42 : 1;
}
