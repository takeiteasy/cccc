// CCCC_FLAGS: --testing
// Suite: ConstIntValue reads integer attribute and macro arguments, run on the
// VM and under -c=native.

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

[[cccc::test]] void test_literal(void) {
    AssertEq(weight_of_w3(), 3);
}

[[cccc::test]] void test_negative(void) {
    AssertEq(weight_of_w_neg(), -2);
}

[[cccc::test]] void test_enum_expression(void) {
    AssertEq(weight_of_w_expr(), 8);
}

[[cccc::test]] void test_cast(void) {
    AssertEq(weight_of_w_cast(), 5);
}

[[cccc::test]] void test_macro_argument(void) {
    AssertEq(plus_one(N * 10), 41);
}
