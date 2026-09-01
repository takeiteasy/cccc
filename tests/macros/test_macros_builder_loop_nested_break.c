// Ticket #1249: a builder loop (MakeWhile) nested inside a Quote()-built
// outer loop must keep its own break/continue targets distinct from the
// outer loop's -- pushing/restoring vm->compiler.brk_label/cont_label around
// the inner QuoteLazy() expansion must not leak into, or get clobbered by,
// the outer loop's own labels.
//
// The inner while(1) loop always breaks on its first (only) iteration, so
// inner_hits increases by exactly 1 per outer iteration if (and only if) the
// inner `break` binds to the inner loop -- if it mistakenly bound to the
// outer loop instead, the outer loop would stop after a single iteration
// and outer_hits would be 1, not 5.
//
// Each $N splice argument below is its own MakeVarRef() call: reusing the
// same Node* pointer at more than one splice site aliases it into the tree
// twice, which codegen rejects ("unsupported expression node kind") -- this
// is a general builder-API rule, not specific to loops.

[[cccc::comptime]]
void gen(void) {
    GlobalVar("outer_hits", GetType("int"));
    GlobalVar("inner_hits", GetType("int"));

    Obj *fn = MakeFunction("f", GetType("int"));
    WithFn(fn) {
        Node *inner = MakeWhile(
            MakeIntLiteral(1),
            QuoteLazy("$1++; break;", MakeVarRef("inner_hits")));

        FunctionSetBody(
            fn,
            Quote("{ for (int i = 0; i < 10; i++) { "
                  "$1++; $2; if ($3 == 5) break; } return $4 * 100 + $5; }",
                  MakeVarRef("outer_hits"), inner, MakeVarRef("outer_hits"),
                  MakeVarRef("outer_hits"), MakeVarRef("inner_hits")));
    }
}
gen();

int main(void) {
    return f() == 505 ? 42 : 1;
}
