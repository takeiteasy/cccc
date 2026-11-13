// A generated function returning long long agrees with a later prototype.
@comptime void gen(void) {
    Obj *fn = MakeFunction("big", GetType("unsigned long long"));
    FunctionAddParam(fn, "x", GetType("double"));
    FunctionSetBody(fn, MakeReturn(MakeIntLiteral(41)));
    Obj *sn = MakeFunction("sbig", GetType("long long"));
    FunctionSetBody(sn, MakeReturn(MakeIntLiteral(1)));
}
gen();
unsigned long long big(double);
long long          sbig(void);
int main(void) { return (int)(big(1.0) + (unsigned long long)sbig()); }
