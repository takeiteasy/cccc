// Ticket #235: Memcpy/Strlen/Strcmp thin AST wrappers over <string.h>.

[[cccc::comptime]]
void generate_stdlib_wrappers(void) {
    Obj *cpy = MakeFunction("wrap_memcpy", MakePointer(GetType("void")));
    FunctionAddParam(cpy, "dst", MakePointer(GetType("void")));
    FunctionAddParam(cpy, "src", MakePointer(GetType("void")));
    FunctionAddParam(cpy, "n", GetType("size_t"));
    WithFn(cpy) {
        FunctionSetBody(cpy, MakeReturn(Memcpy(MakeParamRef(cpy, "dst"),
                                               MakeParamRef(cpy, "src"),
                                               MakeParamRef(cpy, "n"))));
    }

    Obj *len = MakeFunction("wrap_strlen", GetType("size_t"));
    FunctionAddParam(len, "s", MakePointer(GetType("char")));
    WithFn(len) {
        FunctionSetBody(len, MakeReturn(Strlen(MakeParamRef(len, "s"))));
    }

    Obj *cmp = MakeFunction("wrap_strcmp", GetType("int"));
    FunctionAddParam(cmp, "a", MakePointer(GetType("char")));
    FunctionAddParam(cmp, "b", MakePointer(GetType("char")));
    WithFn(cmp) {
        FunctionSetBody(cmp, MakeReturn(Strcmp(MakeParamRef(cmp, "a"),
                                               MakeParamRef(cmp, "b"))));
    }
}

generate_stdlib_wrappers();

int main(void) {
    char src[6] = "hello";
    char dst[6] = {0};

    wrap_memcpy(dst, src, 6);
    if (wrap_strcmp(dst, "hello") != 0)
        return 1;
    if ((int)wrap_strlen(dst) != 5)
        return 2;

    return 42;
}
