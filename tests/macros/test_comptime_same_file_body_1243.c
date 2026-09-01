// #1243: a [[cccc::comptime]] body may call an ordinary function defined
// later in the same file, with no routing at all. Before the on-demand body
// sweep the comptime declaration index resolved only runtimeadd's signature,
// and the call failed at macro-bytecode patch time.

[[cccc::comptime]]
void gen(void) {
    Obj *fn = MakeFunction("f", GetType("int"));
    FunctionSetBody(fn, MakeReturn(MakeIntLiteral(runtimeadd(20, 22))));
}
gen();

int runtimeadd(int a, int b) {
    return a + b;
}

int main(void) {
    return f();
}
