// A synthesized <stddef.h> include makes GetType("size_t") resolve under
// -c=native.
[[cccc::comptime]]
void generate_typedef_wrappers_1057(void) {
    Obj *sz = MakeFunction("get_size_1057", GetType("size_t"));
    WithFn(sz) {
        FunctionSetBody(sz, MakeReturn(MakeIntLiteral(1057)));
    }

    Obj *pd = MakeFunction("get_ptrdiff_1057", GetType("ptrdiff_t"));
    WithFn(pd) {
        FunctionSetBody(pd, MakeReturn(MakeIntLiteral(-7)));
    }

    Obj *wc = MakeFunction("get_wchar_1057", GetType("wchar_t"));
    WithFn(wc) {
        FunctionSetBody(wc, MakeReturn(MakeIntLiteral(97)));
    }
}

generate_typedef_wrappers_1057();

int main(void) {
    if (get_size_1057() != 1057)
        return 1;
    if (get_ptrdiff_1057() != -7)
        return 2;
    if (get_wchar_1057() != 97)
        return 3;
    return 42;
}
