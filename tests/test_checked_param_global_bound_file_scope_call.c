// CCCC_FLAGS: --checked-pointers
// A parameter bound naming a file global resolves in a file that also has a
// file-scope generator call.

int g = 2;

[[cccc::comptime]]
void gen(void) {
    Obj *fn = MakeFunction("generated", GetType("int"));
    FunctionSetBody(fn, MakeReturn(MakeIntLiteral(40)));
}
gen();

int sum(int *[[cccc::array, cccc::count(g)]] p) {
    return p[0] + p[1];
}

int main(void) {
    int a[2] = {1, 1};
    return sum(a) + generated();
}
