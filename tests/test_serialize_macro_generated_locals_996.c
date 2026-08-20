// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: int gen_locals_996\(void\) \{\s*\n\s*int
// (x|y);\s*\n\s*int (x|y);
//
// Ticket #996: a MakeFunction()+FunctionSetBody(fn, Quote(...)) body built
// without WithFn(fn) never attached its Quote()-declared locals to fn, so
// fn->locals stayed empty and -m/-c=native emitted bare assignments to
// undeclared variables (`{ x = 40; }`, no `int x;` anywhere) instead of real
// declarations -- the host compiler would reject that as -c=native output,
// and under plain -m it silently referenced whatever happened to be in
// scope. This asserts the generated function's body opens with both locals
// actually declared.

[[cccc::comptime]]
void gen(void) {
    Obj *fn = MakeFunction("gen_locals_996", GetType("int"));
    FunctionSetBody(fn, Quote("{ int x = 40; int y = 2; return x + y; }"));
    PublishNode(fn);
}
gen();

int gen_locals_996(void);

int main(void) {
    return gen_locals_996();
}
