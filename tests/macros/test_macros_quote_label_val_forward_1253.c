// A GNU labels-as-values `&&label` taken inside a Quote() template, where the
// target label is defined later in the same template, must resolve to the
// label's real code address so a subsequent `goto *p` lands on it. Before,
// nothing resolved a template's labels (comptime expansion is a post-parse
// walk, run after resolve_goto_labels already cleared its worklists), so the
// LTA3 kept offset 0 and `goto *p` crashed with a spurious STACK OVERFLOW.
//
// This is the literal ticket repro.

[[cccc::comptime]]
Node *g(Node *unused) {
    return Quote("{ void *p = &&done; goto *p; return 1; done: return 42; }");
}

int test(void) {
    g(0);
}

int main(void) {
    return test();
}
