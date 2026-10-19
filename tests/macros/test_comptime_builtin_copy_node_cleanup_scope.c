// Copied loops, gotos and switches spliced into a cleanup scope run the cleanup once.
int g, v, cnt;
void bump(int *p) { (void)p; cnt++; }

[[cccc::comptime]]
Node *probe(void) {
    Node *loop = Quote("for (;;) { g += 15; break; }");
    Node *jump = Quote("{ goto out; g += 100; out: g += 1; }");
    Node *sw   = Quote("switch (v) { case 1: g += 5; break; default: g += 100; }");
    Node *s[6] = {loop, CopyNode(loop), jump, CopyNode(jump), sw, CopyNode(sw)};
    return Quote("{ __attribute__((cleanup(bump))) int c = 0; $1; }",
                 __builtin_ast_block(s, 6));
}

int main(void) {
    v = 1;
    probe();
    return (g == 42 && cnt == 1) ? 42 : 10 + cnt;
}
