// Ticket #1242: cc_quote_expand_lazy() (src/reflection.c) re-enters
// quote_core reentrantly, so a QuoteLazy() fragment can itself splice
// another QuoteLazy() fragment -- verify that lazy-in-lazy composition
// works and the innermost fragment's `break` still binds to the outermost
// (real) loop.

[[cccc::comptime]]
void gen(void) {
    Obj *fn = MakeFunction("f", GetType("int"));
    WithFn(fn) {
        Node *inner = QuoteLazy("if (i == 3) break;");
        Node *mid   = QuoteLazy("{ $1; }", inner);
        Node *loop =
            Quote("int i; for (i = 0; i < 10; i++) { $1; } return i;", mid);
        FunctionSetBody(fn, loop);
    }
}
gen();

int main(void) {
    return f() == 3 ? 42 : 1;
}
