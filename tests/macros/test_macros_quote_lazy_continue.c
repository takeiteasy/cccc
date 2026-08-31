// Ticket #1242: `continue` is checked against vm->compiler.cont_label, a
// separate code path from `break`'s brk_label check (src/parse_stmt.c) --
// verify it independently, composed the same way as the break test (a
// QuoteLazy() loop body spliced into an outer Quote()-built for loop), and
// also verify the lazy fragment can reference the outer accumulator (`sum`)
// declared only in the outer template.

[[cccc::comptime]]
void gen(void) {
    Obj *fn = MakeFunction("f", GetType("int"));
    WithFn(fn) {
        Node *body = QuoteLazy("if (i % 2 == 0) continue; sum += i;");
        Node *loop =
            Quote("int i; int sum = 0; for (i = 0; i < 10; i++) { $1; } "
                  "return sum;",
                  body);
        FunctionSetBody(fn, loop);
    }
}
gen();

int main(void) {
    // odd i in [0,10): 1+3+5+7+9 == 25
    return f() == 25 ? 42 : 1;
}
