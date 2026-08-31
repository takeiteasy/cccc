// Ticket #1242: Quote() validates break/continue and variable scope against
// whatever parser context is live when it is called, not the context a
// spliced-in fragment ends up in -- so a loop body built as its own Quote()
// call could not use `break` or reference a variable only declared by a
// separately-built outer loop template, even though the fully composed tree
// is valid C. QuoteLazy() defers tokenizing/parsing the fragment until it is
// actually spliced in, at the splice site's own scope and loop labels. This
// is the ticket's own repro.

[[cccc::comptime]]
void gen(void) {
    Obj *fn = MakeFunction("f", GetType("int"));
    WithFn(fn) {
        Node *body = QuoteLazy("if (i == 3) break;");
        Node *loop =
            Quote("int i; for (i = 0; i < 10; i++) { $1; } return i;", body);
        FunctionSetBody(fn, loop);
    }
}
gen();

int main(void) {
    return f() == 3 ? 42 : 1;
}
