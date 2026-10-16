// MakeCast accepts a MakeMember operand.

typedef struct { int id; } R;
[[cccc::comptime]]
void gen(void) {
    Type *ty = GetType("R");
    Obj *fn = MakeFunction("f", GetType("long long"));
    FunctionAddParam(fn, "self", MakePointer(ty));
    WithFn(fn) {
        FunctionSetBody(fn, MakeReturn(MakeCast(MakeMember(MakeUnary(NK_DEREF, MakeParamRef(fn, "self")), "id"), GetType("long long"))));
    }
    PublishNode(fn);
}
gen();
int main(void) { R a = {42}; return (int)f(&a); }
