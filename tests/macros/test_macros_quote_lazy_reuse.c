// Ticket #1242: splicing the same QuoteLazy() result more than once must
// re-parse it at each splice site (cc_quote_expand_lazy() calls quote_core
// fresh every time) rather than sharing one parsed subtree -- otherwise the
// second splice's `break`/`i` would (at best) alias the first loop's own
// unique_label/locals instead of the second loop's. Verify the same lazy
// fragment works correctly when spliced into two separately-built loops.

[[cccc::comptime]]
void gen(void) {
    Obj  *fn1  = MakeFunction("f1", GetType("int"));
    Obj  *fn2  = MakeFunction("f2", GetType("int"));
    Node *body = QuoteLazy("if (i == 3) break;");
    WithFn(fn1) {
        FunctionSetBody(
            fn1,
            Quote("int i; for (i = 0; i < 10; i++) { $1; } return i;", body));
    }
    WithFn(fn2) {
        FunctionSetBody(
            fn2,
            Quote("int i; for (i = 0; i < 20; i++) { $1; } return i;", body));
    }
}
gen();

int main(void) {
    return (f1() == 3 && f2() == 3) ? 42 : 1;
}
