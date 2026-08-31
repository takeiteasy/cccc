// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: (?=[\s\S]*for \(i = 0; i < 10;)(?=[\s\S]*if \(i == 3\))(?=[\s\S]*break;)
//
// Ticket #1242: a QuoteLazy() loop body (with `break`) composed into an
// outer Quote()-built for loop must survive the -c=generated/-m serializer
// pass as ordinary C, not just the VM execution path already exercised by
// tests/macros/test_macros_quote_lazy_loop_body.c. This is a pure shape
// assertion (dumped source, not compiled/linked/run) -- the VM-execution
// round trip for this exact composition lives in that macros/ test.

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
