// Composition: a QuoteLazy() fragment that is just `goto done;` is spliced at
// a $N inside an eager Quote() template whose body defines `done:`. The
// fragment defines no `done:` of its own, so its goto is a free reference; it
// binds to the outer template's `done:` after both end up in the same spliced
// function body (cc_resolve_body_label_refs, run per-function once macro
// expansion has assembled the tree). Intentional inner-free-ref /
// outer-label composition, not label leakage.

[[cccc::comptime]]
Node *gen(Node *unused) {
    Node *frag = QuoteLazy("goto done;");
    return Quote("{ $1; return 1; done: return 42; }", frag);
}

int test(void) {
    gen(0);
}

int main(void) {
    return test();
}
