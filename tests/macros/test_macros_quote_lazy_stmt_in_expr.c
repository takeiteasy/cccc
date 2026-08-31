// EXPECT_COMPILE_ERROR
// Ticket #1242: a QuoteLazy() fragment that is itself a statement (an `if`,
// here -- deliberately not `break`, which would fail for the unrelated
// reason of having no enclosing loop at all) cannot be spliced in expression
// position -- verify this is a clean diagnostic, not a crash.
// cc_quote_expand_lazy() (src/reflection.c) raises this from the
// want_stmt == false branch; that branch's ND_QUOTE_LAZY carries
// lazy->tok == vm->compiler.macro_call_tok, which can be NULL for a
// fragment captured outside any macro call context.

[[cccc::comptime]]
Node *bad(void) {
    Node *frag = QuoteLazy("if (1) 1;");
    // "$1 + 0;" puts $1 in expression (binary-operand) position.
    return Quote("$1 + 0;", frag);
}

int main(void) {
    bad();
    return 42;
}
