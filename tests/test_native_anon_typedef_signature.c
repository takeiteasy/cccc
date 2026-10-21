// A function over an anonymous typedef round-trips under -c=native.
typedef struct {
    int a;
    int b;
} Pair;
typedef struct {
    int scale;
} Ctx;
Pair mk_pair(Ctx *c);
Pair mk_pair(Ctx *c) {
    Pair p;
    p.a = c->scale;
    p.b = c->scale * 2;
    return p;
}
[[cccc::comptime]]
void generate_anon_typedef_wrapper_18(void) {
    Obj *fn = MakeFunction("wrap_pair_18", GetType("Pair"));
    FunctionAddParam(fn, "c", MakePointer(GetType("Ctx")));
    WithFn(fn) {
        FunctionSetBody(fn,
                        Quote("return mk_pair($1);", MakeParamRef(fn, "c")));
    }
}
generate_anon_typedef_wrapper_18();
int main(void) {
    Ctx  c = {20};
    Pair p = wrap_pair_18(&c);
    if (p.a != 20 || p.b != 40)
        return 1;
    return 42;
}
