// Ticket #1249: verify MakeFor and MakeDoWhile assign their own brk_label/
// cont_label the same way MakeWhile does (test_macros_builder_loop_lazy_
// break.c covers MakeWhile), exercising both `break` and `continue` in each.
//
// A generated-program global (GlobalVar) is used as the shared counter
// instead of a runtime-TU global or a textual loop variable: a QuoteLazy()
// fragment and the outer template that splices it are separate quote_core
// parses, so a name written only as plain text in one has no way to resolve
// in the other (find_var() walks scope, not vm->compiler.locals) -- passing
// the same MakeVarRef() node as a $N argument to each gives every splice
// site its own reference to the identical Obj instead.

[[cccc::comptime]]
void gen(void) {
    GlobalVar("for_cnt", GetType("int"));
    Obj *fn_for = MakeFunction("f_for", GetType("int"));
    WithFn(fn_for) {
        // Counts up, skipping the count of 3 via `continue`, stopping at 5
        // via `break` -- final value is 5, not 6, proving both resolved to
        // this loop's own labels.
        Node *loop = MakeFor(
            NULL, MakeIntLiteral(1), NULL,
            QuoteLazy("$1++; if ($1 == 3) continue; if ($1 == 5) break;",
                      MakeVarRef("for_cnt")));
        FunctionSetBody(fn_for,
                        Quote("{ $1; return $2; }", loop, MakeVarRef("for_cnt")));
    }

    GlobalVar("do_cnt", GetType("int"));
    Obj *fn_do = MakeFunction("f_do", GetType("int"));
    WithFn(fn_do) {
        Node *loop = MakeDoWhile(
            QuoteLazy("$1++; if ($1 == 4) continue; if ($1 == 6) break;",
                      MakeVarRef("do_cnt")),
            MakeIntLiteral(1));
        FunctionSetBody(fn_do,
                        Quote("{ $1; return $2; }", loop, MakeVarRef("do_cnt")));
    }
}
gen();

int main(void) {
    if (f_for() != 5)
        return 1;
    if (f_do() != 6)
        return 2;
    return 42;
}
