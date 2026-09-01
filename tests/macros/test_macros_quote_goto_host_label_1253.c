// A `goto` written in a Quote() template may target a label defined by the
// enclosing function it is spliced into: the template's goto is a free
// reference that cc_resolve_body_label_refs() binds after macro expansion,
// walking the assembled function body.

[[cccc::comptime]]
Node *jump_done(Node *unused) {
    return Quote("goto done;");
}

int test(void) {
    jump_done(0); // statement position -- splices the goto in directly
    return 1;
    // The only `goto done` is the one the template splices in, which binds
    // after parse-time -Wunused analysis has already run; mark the label so
    // that pass doesn't flag it.
done:
    __attribute__((unused));
    return 42;
}

int main(void) {
    return test();
}
