// ConstIntValue reads integer attribute and macro arguments.

enum { N = 4 };

@comptime(attribute("weight")) void define_weight(AttrTarget *target, ...) {
    int64_t w = 0;
    if (VarargCount() != 1 || !ConstIntValue(VarargAt(0), &w))
        MacroErrorAt(MakeIntLiteral(0), "@weight expects an integer constant");
    char name[64];
    strcpy(name, "weight_of_");
    strcat(name, AttrTargetName(target));
    Obj *fn = MakeFunction(name, GetType("int"));
    WithFn(fn) {
        FunctionSetBody(fn, MakeReturn(MakeIntLiteral(w)));
    }
    PublishNode(fn);
}

@comptime Node *plus_one(Node *a) {
    int64_t v = 0;
    if (!ConstIntValue(a, &v))
        MacroErrorAt(a, "plus_one expects an integer constant");
    return MakeIntLiteral(v + 1);
}

@weight(3) int w3;
@weight(-2) int w_neg;
@weight(N * 2) int w_expr;
@weight((int)5) int w_cast;

int main(void) {
    if (weight_of_w3() != 3 || weight_of_w_neg() != -2 ||
        weight_of_w_expr() != 8 || weight_of_w_cast() != 5)
        return 1;
    if (plus_one(N * 10) != 41)
        return 2;
    return 42;
}
