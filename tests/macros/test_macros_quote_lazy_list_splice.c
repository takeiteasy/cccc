// EXPECT_COMPILE_ERROR
// Ticket #1242: a $@N list splice expects an already-parsed ->next-linked
// node chain (NodeList()/splice_chain_for); a QuoteLazy() fragment is
// neither, so binding one to $@N must be rejected up front (in quote_core's
// splice-placeholder registration loop, src/reflection.c) rather than
// failing confusingly deep inside quote_substitute's body-splice walk.

[[cccc::comptime]]
Node *bad(void) {
    Node *lazy = QuoteLazy("x++;");
    return Quote("{ $@1; }", lazy);
}

int main(void) {
    int x = 0;
    bad();
    return x;
}
