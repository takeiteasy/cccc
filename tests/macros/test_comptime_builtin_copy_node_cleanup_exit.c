// A return and a break inside a copied cleanup block unwind the copy's own
// variable.
int cnt, calls;
void release(int *p) {
    (void)p;
    cnt++;
}

[[cccc::comptime]]
Node *probe(void) {
    Node *s    = Quote("{ __attribute__((cleanup(release))) int x = 0;"
                       " if (++calls == 2) return 7; cnt += 20; }");
    Node *x[2] = {s, CopyNode(s)};
    return __builtin_ast_block(x, 2);
}

[[cccc::comptime]]
Node *loop(void) {
    Node *s    = Quote("for (;;) { __attribute__((cleanup(release))) int y = 0;"
                       " cnt += 100; break; }");
    Node *x[2] = {s, CopyNode(s)};
    return __builtin_ast_block(x, 2);
}

int f(void) {
    probe();
    return 0;
}

int main(void) {
    int r = f();
    if (r != 7 || cnt != 22)
        return 10 + cnt;
    cnt = 0;
    loop();
    return cnt == 202 ? 42 : 11;
}
