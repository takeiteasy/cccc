// A QuoteLazy() fragment that is a computed goto (`goto *p;`) spliced at a
// statement-position $N must splice in directly, not be wrapped in a
// spurious ND_EXPR_STMT. cc_quote_expand_lazy()'s want_stmt arm asks
// node_is_stmt_kind(), which now returns true for ND_GOTO_EXPR. Before, the
// wrapped node reached codegen and failed with "unsupported expression node
// kind 34".
//
// The template also exercises label resolution inside a Quote() template:
// `goto skip` (a named jump), `&&spin` (a label address taken before the
// label), and the spliced `goto *p`. `witness` is set on the fall-through
// path into `skip:` and checked after the computed goto lands, so this test
// fails loudly if `goto skip` ever silently no-ops again (it used to: a
// template goto emitted no jump at all, control fell straight into `spin:`
// and `goto *p` never ran).

[[cccc::comptime]]
Node *gen(void) {
    Node *frag = QuoteLazy("goto *p;");
    return Quote("{ int r = 1; int witness = 0; void *p; p = &&spin;"
                 "  goto skip;"
                 "  spin: return (r == 42 && witness == 1) ? 42 : 1;"
                 "  skip: witness = 1; r = 42; $1; return 2; }",
                 frag);
}

int test(void) {
    gen();
}

int main(void) {
    return test();
}
