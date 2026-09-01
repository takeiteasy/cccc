// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: no body is available to the comptime pass
//
// #1243: the on-demand body sweep only forwards a function that is actually
// *defined* in one of cccc's input files. A function that is merely
// declared -- here needs_link, called transitively from the comptime body
// of forwarded() -- still fails, now with a diagnostic that names the two
// ways to make the body reachable instead of the bare "undefined function
// in macro bytecode".
int needs_link(int n);

int forwarded(int n) {
    return needs_link(n) + 1;
}

[[cccc::comptime]]
void gen(void) {
    Obj *fn = MakeFunction("f", GetType("int"));
    FunctionSetBody(fn, MakeReturn(MakeIntLiteral(forwarded(41))));
}
gen();

int main(void) {
    return f();
}
