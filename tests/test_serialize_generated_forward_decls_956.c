// CCCC_FLAGS: -c=generated -o /dev/stdout
// CCCC_EXPECT_STDOUT: gen_c\(int x\);
// CCCC_EXPECT_STDOUT: gen_b\(int x\);(.|\n)*gen_a\(int x\) \{
// CCCC_HOST_CC
// -c=generated forward-declares a generated function's callee published
// earlier, and keeps a body-less published prototype.

[[cccc::comptime]]
void gen(void) {
    Type *t = GetType("int");
    Obj  *c = FunctionPrototype("gen_c", t);
    FunctionAddParam(c, "x", t);
    PublishNode(c);

    Obj *a = MakeFunction("gen_a", t);
    FunctionAddParam(a, "x", t);
    Obj *b = MakeFunction("gen_b", t);
    FunctionAddParam(b, "x", t);
    PublishNode(a);
    PublishNode(b);
    WithFn(a) {
        Node *xr = MakeParamRef(a, "x");
        FunctionSetBody(a, Quote("{ return gen_b($1); }", xr));
    }
    WithFn(b) {
        Node *xr = MakeParamRef(b, "x");
        FunctionSetBody(b, Quote("{ return gen_a($1); }", xr));
    }
}
gen();

int main(void) {
    return 42;
}
