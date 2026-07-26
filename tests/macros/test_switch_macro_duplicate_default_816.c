// #816: __builtin_ast_switch_set_default bypasses the parser entirely, so a
// second SwitchSetDefault must be rejected itself instead of silently
// overwriting the first default label -- same bug class as #815, just
// reachable from comptime macros.
// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: multiple default labels in one switch

[[cccc::comptime]]
Node *make_dup_default_switch(void) {
    Type *int_ty = GetType("int");
    Obj *fn = MakeFunction("dup_default_switch", int_ty);
    FunctionAddParam(fn, "x", int_ty);

    WithFn(fn) {
        Node *sw = MakeSwitch(MakeParamRef(fn, "x"));
        SwitchAddCase(sw, MakeIntLiteral(1), MakeReturn(MakeIntLiteral(10)));
        SwitchSetDefault(sw, MakeReturn(MakeIntLiteral(-1)));
        SwitchSetDefault(sw, MakeReturn(MakeIntLiteral(-2)));
        FunctionSetBody(fn, sw);
    }

    return MakeIntLiteral(0);
}
make_dup_default_switch();

int main(void) {
    return 42;
}
