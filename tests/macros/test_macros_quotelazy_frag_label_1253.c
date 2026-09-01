// A QuoteLazy() fragment carrying a label and a `&&label` reference, spliced
// at a $N inside an eager Quote() template, resolves its own label. The
// enclosing template also has its own goto/label pair. Both must work -- this
// guards the hygienic in-template pass against disturbing normal resolution
// when a lazy fragment is materialised during another template's parse.

[[cccc::comptime]]
Node *gen(Node *unused) {
    Node *frag = QuoteLazy("{ void *q = &&fdone; goto *q; return 1; fdone: ; }");
    return Quote("{ int hit = 0; goto hstart;"
                 "  hbad: return 2;"
                 "  hstart: hit = 1; $1;"
                 "  return hit == 1 ? 42 : 3; }",
                 frag);
}

int test(void) {
    gen(0);
}

int main(void) {
    return test();
}
