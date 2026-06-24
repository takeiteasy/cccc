// Test __builtin_gensym and Gensym for collision-safe generated names.

[[cccc::comptime]]
Node *gensym_test(void) {
    const char *a = Gensym("helper");
    const char *b = __builtin_gensym(VM, "helper");

    int same = 1;
    for (int i = 0; a[i] || b[i]; i++) {
        if (a[i] != b[i]) {
            same = 0;
            break;
        }
    }

    Type *int_ty = GetType("int");
    Obj *fn_a = MakeFunction(a, int_ty);
    Obj *fn_b = MakeFunction(b, int_ty);
    FunctionSetBody(fn_a, MakeReturn(MakeIntLiteral(1)));
    FunctionSetBody(fn_b, MakeReturn(MakeIntLiteral(2)));

    if (same || !fn_a || !fn_b || a[0] != 'h' || b[0] != 'h')
        return MakeIntLiteral(1);
    return MakeIntLiteral(42);
}

int main(void) {
    return gensym_test();
}
