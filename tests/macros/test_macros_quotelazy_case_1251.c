// A single-statement QuoteLazy() fragment that is a `case` label (ND_CASE,
// not brace-wrapped into an ND_BLOCK) spliced at a statement-position $N
// inside a Quote()d switch body. cc_quote_expand_lazy()'s want_stmt arm now
// classifies ND_CASE as a statement (node_is_stmt_kind(), matching
// gen_stmt()), so the case node splices in unwrapped and registers against
// the enclosing switch. Before, it reached codegen wrapped in an
// ND_EXPR_STMT and failed with "unsupported expression node kind 31".

[[cccc::comptime]]
Node *gen(void) {
    Node *frag = QuoteLazy("case 1: r = 42;");
    return Quote("{ int r = 0; int x = 1;"
                 "  switch (x) { $1; break; default: r = 100; }"
                 "  return (r == 42) ? 42 : r; }",
                 frag);
}

int test(void) {
    gen();
}

int main(void) {
    return test();
}
