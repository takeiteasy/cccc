// A copied cleanup variable runs its handler once per copy.
int cnt;
void release(int *p) {
    (void)p;
    cnt++;
}

[[cccc::comptime]]
Node *probe(void) {
    Node *s =
        Quote("{ __attribute__((cleanup(release))) int x = 1; cnt += 20; }");
    Node *x[2] = {s, CopyNode(s)};
    return __builtin_ast_block(x, 2);
}

int main(void) {
    probe();
    return cnt == 42 ? 42 : 10 + cnt;
}
