// Ticket #1249: MakeWhile/MakeFor/MakeDoWhile (the __builtin_ast_while/_for/
// _do_while builder APIs) never assigned node->brk_label/cont_label, so a
// QuoteLazy() body attached to a builder loop had nothing for its break/
// continue to bind to at expansion time. MakeWhile now assigns its own
// unique labels and expands a QuoteLazy() body under them -- this is the
// ticket's own repro, written with QuoteLazy() instead of an eager Quote()
// (see test_macros_builder_loop_withloop.c for why the eager form needs
// WithLoop/LoopSetBody instead).

[[cccc::comptime]]
void gen(void) {
    Obj *fn = MakeFunction("f", GetType("int"));
    WithFn(fn) {
        Node *loop = MakeWhile(MakeIntLiteral(1), QuoteLazy("{ break; }"));
        FunctionSetBody(fn, Quote("{ $1; return 0; }", loop));
    }
}
gen();

int main(void) {
    return f() == 0 ? 42 : 1;
}
