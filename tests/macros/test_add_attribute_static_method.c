// AddAttribute binds a generated function as a static method.
typedef struct Plain {
    int a;
} Plain;

[[cccc::comptime]]
void gen_static_method(void) {
    Obj *fn = MakeFunction("Plain_make", GetType("Plain"));
    WithFn(fn) {
        FunctionSetBody(fn, Quote("return (Plain){42};"));
    }
    PublishNode(fn);
    AddAttribute(fn, "cccc::static_method(Plain, make)");
}
gen_static_method();

int main(void) {
    Plain p = Plain.make();
    return p.a;
}
