// Ticket #1242: a QuoteLazy() fragment ($2) coexisting with a $@N list-splice
// chain ($@1) in the same outer template. Verifies the lazy capture path's
// arena-allocated array (Node.lazy_args -- see the ND_QUOTE_LAZY comment in
// src/cccc.h) never disturbs the ->next links a $@N chain relies on
// (NodeList()/splice_chain_for, src/reflection.c) -- the two mechanisms
// share the same `nodes` array passed into quote_core, but must not
// interfere with each other.

[[cccc::comptime]]
Node *gen(Node *acc) {
    Node *s1    = Quote("$1 += 1;", acc);
    Node *chain = NodeList((Node *[]){s1}, 1);
    Node *body  = QuoteLazy("if (i == 2) break;");
    return Quote("for (int i = 0; i < 10; i++) { $@1; $2; }", chain, body);
}

int test(void) {
    int acc = 0;
    gen(acc);
    return acc;
}

int main(void) {
    // The chain ($@1) runs before the lazy break check ($2) each iteration:
    // i==0 -> acc=1; i==1 -> acc=2; i==2 -> acc=3, then break.
    return test() == 3 ? 42 : 1;
}
