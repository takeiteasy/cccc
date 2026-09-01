// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: (?=[\s\S]*for \(; 1LL; \))(?=[\s\S]*if \(i == 3\))(?=[\s\S]*break;)
//
// Ticket #1249: a builder-built loop (MakeWhile, not a parser-built for/
// while) with a QuoteLazy() body containing `break` must survive the
// -c=generated/-m serializer pass as ordinary C, mirroring
// tests/test_serialize_quote_lazy_loop_1242.c (which covers a parser-built
// Quote() for-loop with a spliced QuoteLazy() body). This is a pure shape
// assertion (dumped source, not compiled/linked/run) -- the VM-execution
// round trip for this exact composition lives in
// tests/macros/test_macros_builder_loop_lazy_break.c.

[[cccc::comptime]]
void gen(void) {
    GlobalVar("i", GetType("int"));
    Obj *fn = MakeFunction("f", GetType("int"));
    WithFn(fn) {
        Node *loop = MakeWhile(
            MakeIntLiteral(1),
            QuoteLazy("$1++; if ($2 == 3) break;", MakeVarRef("i"),
                      MakeVarRef("i")));
        FunctionSetBody(fn, Quote("{ $1; return $2; }", loop, MakeVarRef("i")));
    }
}
gen();

int main(void) {
    return f() == 3 ? 42 : 1;
}
