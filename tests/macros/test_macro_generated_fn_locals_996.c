// Ticket #996: a MakeFunction()+FunctionSetBody(fn, Quote(...)) body built
// WITHOUT wrapping in WithFn(fn) used to leave every local declared inside
// the Quote() template unattached to fn->locals -- they all kept the
// default offset of 0 and aliased the same frame slot. This repro has no
// block literal at all (the ticket's own repro did, which made it look
// block-specific); before the fix this crashed with SIGBUS (exit 138), not
// a compile error, so it's the load-bearing proof the bug -- and the fix --
// aren't block-specific.

[[cccc::comptime]]
void gen(void) {
    Obj *fn = MakeFunction("gen_add", GetType("int"));
    FunctionSetBody(fn, Quote("{ int x = 40; int y = 2; return x + y; }"));
    PublishNode(fn);
}
gen();

int gen_add(void);

int main(void) {
    return gen_add();
}
