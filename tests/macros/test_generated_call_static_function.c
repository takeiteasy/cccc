// A comptime-generated function can call a static function of the program.

static int add_one(int a) {
    return a + 1;
}

[[cccc::comptime]]
void gen(void) {
    Obj *fn = MakeFunction("add_two", GetType("int"));
    FunctionAddParam(fn, "x", GetType("int"));
    WithFn(fn) {
        Node *args[1] = {MakeParamRef(fn, "x")};
        Node *inner   = MakeFuncCall(MakeVarRef("add_one"), args, 1);
        Node *outer[1] = {inner};
        FunctionSetBody(
            fn, MakeReturn(MakeFuncCall(MakeVarRef("add_one"), outer, 1)));
    }
    PublishNode(fn);
}
gen();

int main(void) {
    return add_two(40);
}
