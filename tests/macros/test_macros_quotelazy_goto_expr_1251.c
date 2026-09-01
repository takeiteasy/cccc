// A QuoteLazy() fragment that is a computed goto (`goto *p;`) spliced at a
// statement-position $N must splice in directly, not be wrapped in a
// spurious ND_EXPR_STMT. cc_quote_expand_lazy()'s want_stmt arm asks
// node_is_stmt_kind(), which now returns true for ND_GOTO_EXPR. Before, the
// wrapped node reached codegen and failed with "unsupported expression node
// kind 34".
//
// The label target is resolved backwards (goto skip; ... spin: ...) rather
// than forwards: a `&&forward_label` taken inside a Quote() template is a
// separate, pre-existing label-resolution bug, tracked independently.

[[cccc::comptime]]
Node *gen(void) {
    Node *frag = QuoteLazy("goto *p;");
    return Quote("{ int r = 1; void *p; p = &&spin; goto skip;"
                 "  spin: r = 42; return r;"
                 "  skip: ; $1; return r; }",
                 frag);
}

int test(void) {
    gen();
}

int main(void) {
    return test();
}
