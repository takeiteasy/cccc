// Ticket #1242: a QuoteLazy() fragment can also be spliced in *expression*
// position (not just statement position) -- the primary() hook in
// src/parse_postfix.c materializes it there, distinct from the expr_stmt()
// hook in src/parse_stmt.c used by the statement-position tests. Here the
// lazy fragment is an expression (`i * 2`) referencing the outer loop
// variable `i`, assigned into `r` on every iteration.

[[cccc::comptime]]
void gen(void) {
    Obj *fn = MakeFunction("f", GetType("int"));
    WithFn(fn) {
        Node *val = QuoteLazy("i * 2");
        Node *loop =
            Quote("int i; int r = 0; for (i = 0; i < 5; i++) { r = $1; } "
                  "return r;",
                  val);
        FunctionSetBody(fn, loop);
    }
}
gen();

int main(void) {
    // last iteration: i == 4, r == 8
    return f() == 8 ? 42 : 1;
}
