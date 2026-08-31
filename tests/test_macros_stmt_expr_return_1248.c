// Ticket #1248: the supported way to get a value out of a multi-statement
// Quote() template used in expression position is a GNU statement
// expression (`Quote("({ ... })")`) -- unlike a bare `{ ... }`, that is a
// value-producing expression node (ND_STMT_EXPR), not a statement, so it
// splices cleanly into `int r = gen();`.

[[cccc::comptime]]
Node *gen(void) {
    return Quote("({ int i; i = 3; i; })");
}

int test(void) {
    int r = gen();
    return (r == 3) ? 42 : 1;
}

int main(void) {
    return test();
}
